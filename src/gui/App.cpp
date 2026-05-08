// src/gui/App.cpp  ─  APB Options 1:1 layout
#include "App.h"
#include "PageGradientMaker.h"
#include "PageWeaponColour.h"
#include "Pages.h"
#include "backend/AppDirs.h"
#include "backend/ThemeLibrary.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdio>

namespace apb::gui {
using nlohmann::json;

// ─── Pages ────────────────────────────────────────────────────────────────
enum PageId {
    PAGE_GRADIENT=0, PAGE_WCOLOUR,
    PAGE_VEHICLE, PAGE_WEAPON,
    PAGE_LOCALIZATION, PAGE_ARMAS, PAGE_PLAYER_ROLES, PAGE_HEX_CONVERTER, PAGE_CREDITS,
    PAGE_CONTACTS,
    PAGE_COUNT
};
static PageGradientMaker      s_gradient;
static PageWeaponColour       s_wcolour;
static PageVehicleItemTypes   s_vehicle;
static PageWeaponItemTypes    s_weapon;
static PageLocalization       s_localization;
static PageArmasScrape        s_armas;
static PagePlayerRoles        s_playerRoles;
static PageHexConverter       s_hexConverter;
static PageCredits            s_credits;
static PageContactDescription s_contacts;
static int s_current = PAGE_GRADIENT;

struct NavItem  { const char* label; int page; };
struct NavGroup { const char* header; std::vector<NavItem> items; bool open=false; };
static std::vector<NavGroup> s_nav = {
    { "Colour Edits", {{"Gradient Maker",PAGE_GRADIENT},{"Weapon Colour",PAGE_WCOLOUR}},      true  },
    { "ItemTypes",    {{"Vehicles",PAGE_VEHICLE},{"Weapons",PAGE_WEAPON}}, false },
    { "Localization", {{"Localization",PAGE_LOCALIZATION}},  false },
    { "Extras",       {{"ARMAS Scanner",PAGE_ARMAS},{"Player Roles",PAGE_PLAYER_ROLES},{"Contact Description",PAGE_CONTACTS},{"Hex Converter",PAGE_HEX_CONVERTER}}, false },
    { "Credits",      {{"Credits",PAGE_CREDITS}},            false },
};
static const char* pageCategory[PAGE_COUNT] = {
    "Colour Edits","Colour Edits",
    "ItemTypes","ItemTypes",
    "Localization","Extras","Extras","Extras","Credits","Extras"
};
static bool s_configLoaded = false;
static std::string s_configStatus;
static bool s_configStatusOk = true;

static bool groupHasPage(const NavGroup& grp, int page);

static void selectStartupPage(){
    s_current = PAGE_GRADIENT;
    for(auto& grp : s_nav){
        grp.open = groupHasPage(grp, s_current);
    }
}

static std::string configPath(){
    return apb::AppDocDir() + "\\config.json";
}

static void setConfigStatus(std::string msg, bool ok){
    s_configStatus = std::move(msg);
    s_configStatusOk = ok;
}

static void copyString(char* dst, size_t dstSize, const std::string& value){
    if(dstSize == 0) return;
    std::snprintf(dst, dstSize, "%s", value.c_str());
}

static std::string jsonEscape(const std::string& value){
    std::string out;
    out.reserve(value.size() + 8);
    for(char ch : value){
        switch(ch){
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += ch; break;
        }
    }
    return out;
}

static void jsonToColor(const json& value, float col[3]){
    if(!value.is_array() || value.size() < 3) return;
    for(int i = 0; i < 3; ++i){
        if(value[i].is_number()) col[i] = (float)value[i].get<double>();
    }
}

static void writeColor(std::ostream& out, const float col[3]){
    out << "[" << col[0] << ", " << col[1] << ", " << col[2] << "]";
}

static std::string themeNameForIndex(int idx){
    auto& lib = ThemeLib();
    return (idx >= 0 && idx < lib.count()) ? lib.themes[idx].name : "Custom";
}

static int themeIndexForName(const std::string& name){
    if(name == "Custom") return ThemeLib().count();
    auto& lib = ThemeLib();
    for(int i = 0; i < lib.count(); ++i){
        if(lib.themes[i].name == name) return i;
    }
    return lib.count();
}

static int jsonInt(const json& obj, const char* key, int def){
    const json& value = obj[key];
    return value.is_number() ? (int)value.get<double>() : def;
}

static bool jsonBool(const json& obj, const char* key, bool def){
    const json& value = obj[key];
    return value.is_boolean() ? value.get<bool>() : def;
}

static std::string jsonString(const json& obj, const char* key, const std::string& def = {}){
    const json& value = obj[key];
    return value.is_string() ? value.get<std::string>() : def;
}

static void writeColourScheme(std::ostream& out, const ColourSchemeWidget& colours, const char* indent){
    out << "{\n";
    out << indent << "  \"scheme_idx\": " << colours.schemeIdx << ",\n";
    out << indent << "  \"preset_equipped\": " << (colours.presetEquipped ? "true" : "false") << ",\n";
    out << indent << "  \"preset_theme\": \"" << jsonEscape(
        (colours.presetThemeIdx >= 0 && colours.presetThemeIdx < ThemeLib().count())
            ? ThemeLib().themes[colours.presetThemeIdx].name
            : std::string{}
    ) << "\",\n";
    out << indent << "  \"single\": ";
    writeColor(out, colours.singleCol);
    out << ",\n";
    out << indent << "  \"gradient_start\": ";
    writeColor(out, colours.gradStart);
    out << ",\n";
    out << indent << "  \"triple_middle\": ";
    writeColor(out, colours.tripleMid);
    out << ",\n";
    out << indent << "  \"gradient_end\": ";
    writeColor(out, colours.gradEnd);
    out << "\n" << indent << "}";
}

static void applyColourSchemeJson(const json& value, ColourSchemeWidget& colours,
    bool allowPresetRestore = true){
    if(!value.is_object()) return;
    colours.schemeIdx = jsonInt(value, "scheme_idx", colours.schemeIdx);
    colours.presetEquipped = jsonBool(value, "preset_equipped", colours.presetEquipped);
    jsonToColor(value["single"], colours.singleCol);
    jsonToColor(value["gradient_start"], colours.gradStart);
    jsonToColor(value["triple_middle"], colours.tripleMid);
    jsonToColor(value["gradient_end"], colours.gradEnd);
    colours.presetThemeIdx = -1;

    const std::string presetTheme = jsonString(value, "preset_theme");
    if(colours.presetEquipped && !presetTheme.empty()){
        for(int i = 0; i < ThemeLib().count(); ++i){
            if(ThemeLib().themes[i].name == presetTheme){
                colours.presetThemeIdx = i;
                break;
            }
        }
        if(colours.presetThemeIdx < 0) colours.presetEquipped = false;
    }

    if(!allowPresetRestore && colours.presetEquipped)
        colours.resetDefaults();
}

static bool saveConfig(){
    try{
        std::filesystem::create_directories(apb::AppDocDir());
        std::ostringstream out;
        out << "{\n";
        out << "  \"current_page\": " << s_current << ",\n";
        out << "  \"nav_open\": [";
        for(size_t i = 0; i < s_nav.size(); ++i){
            if(i) out << ", ";
            out << (s_nav[i].open ? "true" : "false");
        }
        out << "],\n";

        out << "  \"gradient_maker\": {\n";
        out << "    \"theme_hard\": \"" << jsonEscape(themeNameForIndex(s_gradient.themeIdxHard)) << "\",\n";
        out << "    \"hard_col_count\": " << s_gradient.hardColCount << ",\n";
        out << "    \"hard_input\": \"" << jsonEscape(s_gradient.hardInput) << "\",\n";
        out << "    \"hard_output\": \"" << jsonEscape(s_gradient.hardOutput) << "\",\n";
        out << "    \"smooth_input\": \"" << jsonEscape(s_gradient.smoothInput) << "\",\n";
        out << "    \"smooth_output\": \"" << jsonEscape(s_gradient.smoothOutput) << "\",\n";
        out << "    \"triple_input\": \"" << jsonEscape(s_gradient.tripleInput) << "\",\n";
        out << "    \"triple_output\": \"" << jsonEscape(s_gradient.tripleOutput) << "\",\n";
        out << "    \"smooth_start\": "; writeColor(out, s_gradient.smoothStart); out << ",\n";
        out << "    \"smooth_end\": "; writeColor(out, s_gradient.smoothEnd); out << ",\n";
        out << "    \"triple_start\": "; writeColor(out, s_gradient.tripleStart); out << ",\n";
        out << "    \"triple_mid\": "; writeColor(out, s_gradient.tripleMid); out << ",\n";
        out << "    \"triple_end\": "; writeColor(out, s_gradient.tripleEnd); out << ",\n";
        out << "    \"theme_smooth\": \"" << jsonEscape(themeNameForIndex(s_gradient.themeIdxSmooth)) << "\",\n";
        out << "    \"theme_triple\": \"" << jsonEscape(themeNameForIndex(s_gradient.themeIdxTriple)) << "\",\n";
        out << "    \"hard_cols\": [";
        for(int i = 0; i < s_gradient.hardColCount; ++i){
            if(i) out << ", ";
            writeColor(out, s_gradient.hardCols[i]);
        }
        out << "]\n";
        out << "  },\n";

        out << "  \"weapon_colour\": {\n";
        out << "    \"file_path\": \"" << jsonEscape(s_wcolour.filePath) << "\",\n";
        out << "    \"mode_idx\": " << s_wcolour.modeIdx << ",\n";
        out << "    \"font_idx\": " << s_wcolour.fontIdx << ",\n";
        out << "    \"single\": "; writeColor(out, s_wcolour.singleCol); out << ",\n";
        out << "    \"gradient_start\": "; writeColor(out, s_wcolour.gradStart); out << ",\n";
        out << "    \"gradient_end\": "; writeColor(out, s_wcolour.gradEnd); out << ",\n";
        out << "    \"ignore_list\": \"" << jsonEscape(s_wcolour.ignoreList) << "\",\n";
        out << "    \"preset_name\": \"" << jsonEscape(s_wcolour.presetName) << "\",\n";
        out << "    \"preset_mode_idx\": " << s_wcolour.presetModeIdx << ",\n";
        out << "    \"preset_solid\": "; writeColor(out, s_wcolour.presetSolid); out << ",\n";
        out << "    \"preset_smooth_start\": "; writeColor(out, s_wcolour.presetSmoothStart); out << ",\n";
        out << "    \"preset_smooth_end\": "; writeColor(out, s_wcolour.presetSmoothEnd); out << ",\n";
        out << "    \"preset_triple_start\": "; writeColor(out, s_wcolour.presetTripleStart); out << ",\n";
        out << "    \"preset_triple_mid\": "; writeColor(out, s_wcolour.presetTripleMid); out << ",\n";
        out << "    \"preset_triple_end\": "; writeColor(out, s_wcolour.presetTripleEnd); out << ",\n";
        out << "    \"preset_stepped\": [";
        for(int i = 0; i < 6; ++i){
            if(i) out << ", ";
            writeColor(out, s_wcolour.presetStepped[i]);
        }
        out << "]\n";
        out << "  },\n";

        out << "  \"weapon_item_types\": {\n";
        out << "    \"int_path\": \"" << jsonEscape(s_weapon.intPath) << "\",\n";
        out << "    \"out_path\": \"" << jsonEscape(s_weapon.outPath) << "\",\n";
        out << "    \"workers\": " << s_weapon.workers << ",\n";
        out << "    \"colours\": ";
        writeColourScheme(out, s_weapon.colours, "    ");
        out << "\n  },\n";

        out << "  \"vehicle_item_types\": {\n";
        out << "    \"int_path\": \"" << jsonEscape(s_vehicle.intPath) << "\",\n";
        out << "    \"out_path\": \"" << jsonEscape(s_vehicle.outPath) << "\",\n";
        out << "    \"workers\": " << s_vehicle.workers << ",\n";
        out << "    \"colours\": ";
        writeColourScheme(out, s_vehicle.colours, "    ");
        out << "\n  },\n";

        out << "  \"armas\": {\n";
        out << "    \"start_id\": " << s_armas.startId << ",\n";
        out << "    \"end_id\": " << s_armas.endId << ",\n";
        out << "    \"threads\": " << s_armas.threads << ",\n";
        out << "    \"out_path\": \"" << jsonEscape(s_armas.outPath) << "\"\n";
        out << "  },\n";

        out << "  \"player_roles\": {\n";
        out << "    \"int_path\": \"" << jsonEscape(s_playerRoles.intPath) << "\",\n";
        out << "    \"out_path\": \"" << jsonEscape(s_playerRoles.outPath) << "\",\n";
        out << "    \"name_style\": " << s_playerRoles.nameStyleIdx << ",\n";
        out << "    \"use_short_equipment_names\": " << (s_playerRoles.useShortEquipmentNames ? "true" : "false") << ",\n";
        out << "    \"solid\": "; writeColor(out, s_playerRoles.solidCol); out << ",\n";
        out << "    \"gradient_start\": "; writeColor(out, s_playerRoles.gradStart); out << ",\n";
        out << "    \"gradient_middle\": "; writeColor(out, s_playerRoles.gradMiddle); out << ",\n";
        out << "    \"gradient_end\": "; writeColor(out, s_playerRoles.gradEnd); out << "\n";
        out << "  },\n";

        out << "  \"contacts\": {\n";
        out << "    \"int_path\": \"" << jsonEscape(s_contacts.intPath) << "\",\n";
        out << "    \"out_path\": \"" << jsonEscape(s_contacts.outPath) << "\",\n";
        out << "    \"mode\": " << s_contacts.modeIdx << "\n";
        out << "  }\n";
        out << "}\n";

        std::ofstream outFile(configPath(), std::ios::binary | std::ios::trunc);
        if(!outFile) throw std::runtime_error("could not open config file for writing");
        outFile << out.str();
        setConfigStatus("Config saved.", true);
        return true;
    }catch(const std::exception& e){
        setConfigStatus(std::string("Save failed: ") + e.what(), false);
        return false;
    }
}

static void loadConfig(){
    s_configLoaded = true;
    selectStartupPage();
    ThemeLib().reload();
    s_gradient.needsReload = false;
    if(!ThemeLib().themes.empty()){
        s_gradient.applyThemeHard(ThemeLib().themes[0]);
        s_gradient.applyThemeSmooth(ThemeLib().themes[0]);
        s_gradient.applyThemeTriple(ThemeLib().themes[0]);
        s_gradient.themeIdxHard = 0;
        s_gradient.themeIdxSmooth = 0;
        s_gradient.themeIdxTriple = 0;
    }

    std::ifstream in(configPath(), std::ios::binary);
    if(!in){
        setConfigStatus("No saved config found.", true);
        return;
    }

    try{
        std::ostringstream ss;
        ss << in.rdbuf();
        bool ok = false;
        json root = json::parse(ss.str(), ok);
        if(!ok || !root.is_object()) throw std::runtime_error("invalid config JSON");

        if(root.contains("gradient_maker") && root["gradient_maker"].is_object()){
            const json& gradient = root["gradient_maker"];
            s_gradient.themeIdxHard = themeIndexForName(jsonString(gradient, "theme_hard", "Custom"));
            s_gradient.themeIdxSmooth = themeIndexForName(jsonString(gradient, "theme_smooth", "Custom"));
            s_gradient.themeIdxTriple = themeIndexForName(jsonString(gradient, "theme_triple", "Custom"));
            s_gradient.hardColCount = std::clamp(jsonInt(gradient, "hard_col_count", s_gradient.hardColCount), 2, 8);
            copyString(s_gradient.hardInput, sizeof(s_gradient.hardInput), jsonString(gradient, "hard_input"));
            copyString(s_gradient.hardOutput, sizeof(s_gradient.hardOutput), jsonString(gradient, "hard_output"));
            copyString(s_gradient.smoothInput, sizeof(s_gradient.smoothInput), jsonString(gradient, "smooth_input"));
            copyString(s_gradient.smoothOutput, sizeof(s_gradient.smoothOutput), jsonString(gradient, "smooth_output"));
            copyString(s_gradient.tripleInput, sizeof(s_gradient.tripleInput), jsonString(gradient, "triple_input"));
            copyString(s_gradient.tripleOutput, sizeof(s_gradient.tripleOutput), jsonString(gradient, "triple_output"));
            jsonToColor(gradient["smooth_start"], s_gradient.smoothStart);
            jsonToColor(gradient["smooth_end"], s_gradient.smoothEnd);
            jsonToColor(gradient["triple_start"], s_gradient.tripleStart);
            jsonToColor(gradient["triple_mid"], s_gradient.tripleMid);
            jsonToColor(gradient["triple_end"], s_gradient.tripleEnd);
            if(gradient.contains("hard_cols") && gradient["hard_cols"].is_array()){
                int limit = std::min(s_gradient.hardColCount, (int)gradient["hard_cols"].size());
                for(int i = 0; i < limit; ++i)
                    jsonToColor(gradient["hard_cols"][i], s_gradient.hardCols[i]);
            }
        }

        if(root.contains("weapon_colour") && root["weapon_colour"].is_object()){
            const json& wc = root["weapon_colour"];
            copyString(s_wcolour.filePath, sizeof(s_wcolour.filePath), jsonString(wc, "file_path"));
            s_wcolour.modeIdx = std::clamp(jsonInt(wc, "mode_idx", s_wcolour.modeIdx), 0, 1);
            if(!s_wcolour.fonts.empty())
                s_wcolour.fontIdx = std::clamp(jsonInt(wc, "font_idx", s_wcolour.fontIdx), 0, (int)s_wcolour.fonts.size() - 1);
            jsonToColor(wc["single"], s_wcolour.singleCol);
            jsonToColor(wc["gradient_start"], s_wcolour.gradStart);
            jsonToColor(wc["gradient_end"], s_wcolour.gradEnd);
            copyString(s_wcolour.ignoreList, sizeof(s_wcolour.ignoreList), jsonString(wc, "ignore_list"));
            copyString(s_wcolour.presetName, sizeof(s_wcolour.presetName), jsonString(wc, "preset_name", "Custom"));
            s_wcolour.presetModeIdx = std::clamp(jsonInt(wc, "preset_mode_idx", s_wcolour.presetModeIdx), 0, 3);
            jsonToColor(wc["preset_solid"], s_wcolour.presetSolid);
            jsonToColor(wc["preset_smooth_start"], s_wcolour.presetSmoothStart);
            jsonToColor(wc["preset_smooth_end"], s_wcolour.presetSmoothEnd);
            jsonToColor(wc["preset_triple_start"], s_wcolour.presetTripleStart);
            jsonToColor(wc["preset_triple_mid"], s_wcolour.presetTripleMid);
            jsonToColor(wc["preset_triple_end"], s_wcolour.presetTripleEnd);
            if(wc.contains("preset_stepped") && wc["preset_stepped"].is_array()){
                int limit = std::min(6, (int)wc["preset_stepped"].size());
                for(int i = 0; i < limit; ++i)
                    jsonToColor(wc["preset_stepped"][i], s_wcolour.presetStepped[i]);
            }
        }

        if(root.contains("weapon_item_types") && root["weapon_item_types"].is_object()){
            const json& wit = root["weapon_item_types"];
            copyString(s_weapon.intPath, sizeof(s_weapon.intPath), jsonString(wit, "int_path"));
            copyString(s_weapon.outPath, sizeof(s_weapon.outPath), jsonString(wit, "out_path"));
            s_weapon.workers = std::clamp(jsonInt(wit, "workers", s_weapon.workers), 1, 64);
            applyColourSchemeJson(wit["colours"], s_weapon.colours, false);
        }

        if(root.contains("vehicle_item_types") && root["vehicle_item_types"].is_object()){
            const json& vit = root["vehicle_item_types"];
            copyString(s_vehicle.intPath, sizeof(s_vehicle.intPath), jsonString(vit, "int_path"));
            copyString(s_vehicle.outPath, sizeof(s_vehicle.outPath), jsonString(vit, "out_path"));
            s_vehicle.workers = std::clamp(jsonInt(vit, "workers", s_vehicle.workers), 1, 64);
            applyColourSchemeJson(vit["colours"], s_vehicle.colours);
        }

        if(root.contains("armas") && root["armas"].is_object()){
            const json& armas = root["armas"];
            s_armas.startId = jsonInt(armas, "start_id", s_armas.startId);
            s_armas.endId = jsonInt(armas, "end_id", s_armas.endId);
            s_armas.threads = std::clamp(jsonInt(armas, "threads", s_armas.threads), 1, 128);
            copyString(s_armas.outPath, sizeof(s_armas.outPath), jsonString(armas, "out_path"));
        }

        if(root.contains("player_roles") && root["player_roles"].is_object()){
            const json& playerRoles = root["player_roles"];
            copyString(s_playerRoles.intPath, sizeof(s_playerRoles.intPath), jsonString(playerRoles, "int_path"));
            copyString(s_playerRoles.outPath, sizeof(s_playerRoles.outPath), jsonString(playerRoles, "out_path"));
            s_playerRoles.nameStyleIdx = std::clamp(jsonInt(
                playerRoles, "name_style", s_playerRoles.nameStyleIdx), 0, 4);
            s_playerRoles.useShortEquipmentNames = jsonBool(
                playerRoles, "use_short_equipment_names", s_playerRoles.useShortEquipmentNames);
            jsonToColor(playerRoles["solid"], s_playerRoles.solidCol);
            jsonToColor(playerRoles["gradient_start"], s_playerRoles.gradStart);
            jsonToColor(playerRoles["gradient_middle"], s_playerRoles.gradMiddle);
            jsonToColor(playerRoles["gradient_end"], s_playerRoles.gradEnd);
            if(playerRoles.contains("colour_display_names") && playerRoles["colour_display_names"].is_boolean()){
                s_playerRoles.nameStyleIdx = playerRoles["colour_display_names"].get<bool>() ? 1 : 0;
            }
        }

        if(root.contains("contacts") && root["contacts"].is_object()){
            const json& contacts = root["contacts"];
            copyString(s_contacts.intPath, sizeof(s_contacts.intPath), jsonString(contacts, "int_path"));
            copyString(s_contacts.outPath, sizeof(s_contacts.outPath), jsonString(contacts, "out_path"));
            s_contacts.modeIdx = std::clamp(jsonInt(contacts, "mode", s_contacts.modeIdx), 0, 1);
        }

        setConfigStatus("Config loaded.", true);
    }catch(const std::exception& e){
        setConfigStatus(std::string("Load failed: ") + e.what(), false);
    }

    selectStartupPage();
}

static void drawPage(){
    switch(s_current){
        case PAGE_GRADIENT:     s_gradient.draw();     break;
        case PAGE_WCOLOUR:      s_wcolour.draw();      break;
        case PAGE_VEHICLE:      s_vehicle.draw();      break;
        case PAGE_WEAPON:       s_weapon.draw();       break;
        case PAGE_LOCALIZATION: s_localization.draw(); break;
        case PAGE_ARMAS:        s_armas.draw();        break;
        case PAGE_PLAYER_ROLES: s_playerRoles.draw();  break;
        case PAGE_HEX_CONVERTER:s_hexConverter.draw(); break;
        case PAGE_CREDITS:      s_credits.draw();      break;
        case PAGE_CONTACTS:     s_contacts.draw();     break;
    }
}

static bool selectedToolIsRunning(){
    switch(s_current){
        case PAGE_GRADIENT:      return s_gradient.isActionRunning();
        case PAGE_WCOLOUR:       return s_wcolour.isActionRunning();
        case PAGE_VEHICLE:       return s_vehicle.isActionRunning();
        case PAGE_WEAPON:        return s_weapon.isActionRunning();
        case PAGE_ARMAS:         return s_armas.isActionRunning();
        case PAGE_PLAYER_ROLES:  return s_playerRoles.isActionRunning();
        case PAGE_CONTACTS:      return s_contacts.isActionRunning();
        default:                 return false;
    }
}

static bool selectedToolCanStart(){
    switch(s_current){
        case PAGE_GRADIENT:      return s_gradient.canStartAction();
        case PAGE_WCOLOUR:       return s_wcolour.canStartAction();
        case PAGE_VEHICLE:       return s_vehicle.canStartAction();
        case PAGE_WEAPON:        return s_weapon.canStartAction();
        case PAGE_ARMAS:         return s_armas.canStartAction();
        case PAGE_PLAYER_ROLES:  return s_playerRoles.canStartAction();
        case PAGE_CONTACTS:      return s_contacts.canStartAction();
        default:                 return false;
    }
}

static bool selectedToolCanCancel(){
    return selectedToolIsRunning();
}

static void startSelectedTool(){
    switch(s_current){
        case PAGE_GRADIENT:      s_gradient.startAction(); break;
        case PAGE_WCOLOUR:       s_wcolour.startAction();  break;
        case PAGE_VEHICLE:       s_vehicle.startAction();  break;
        case PAGE_WEAPON:        s_weapon.startAction();   break;
        case PAGE_ARMAS:         s_armas.startAction();    break;
        case PAGE_PLAYER_ROLES:  s_playerRoles.startAction(); break;
        case PAGE_CONTACTS:      s_contacts.startAction(); break;
        default: break;
    }
}

static void cancelSelectedTool(){
    switch(s_current){
        case PAGE_GRADIENT:      s_gradient.cancelAction(); break;
        case PAGE_WCOLOUR:       s_wcolour.cancelAction();  break;
        case PAGE_VEHICLE:       s_vehicle.cancelAction();  break;
        case PAGE_WEAPON:        s_weapon.cancelAction();   break;
        case PAGE_ARMAS:         s_armas.cancelAction();    break;
        case PAGE_PLAYER_ROLES:  s_playerRoles.cancelAction(); break;
        case PAGE_CONTACTS:      s_contacts.cancelAction(); break;
        default: break;
    }
}

// ─── Layout constants ──────────────────────────────────────────────────────
static constexpr float TITLE_H     = 58.f;
static constexpr float BOTTOM_H    = 44.f;
static constexpr float RAIL_W      = 228.f;
static constexpr float RAIL_MARGIN = 10.f;
static constexpr float BTN_H       = 34.f;
static constexpr float BTN_GAP     = 4.f;

// ─── APB colour constants ──────────────────────────────────────────────────
static constexpr ImU32 C_APP_BG       = IM_COL32(10,10,10,255);
static constexpr ImU32 C_TITLE_BG     = IM_COL32(14,14,14,255);
static constexpr ImU32 C_SIDE_BG      = IM_COL32(12,12,12,255);
static constexpr ImU32 C_PANEL_BG     = IM_COL32(4,4,4,255);
static constexpr ImU32 C_BOTTOM_BG    = IM_COL32(32,32,32,255);
static constexpr ImU32 C_FRAME_BORDER = IM_COL32(105,105,105,170);
static constexpr ImU32 C_NAV_FILL     = IM_COL32(8,8,8,255);
static constexpr ImU32 C_NAV_HOVER    = IM_COL32(22,22,22,255);
static constexpr ImU32 C_NAV_BORDER   = IM_COL32(120,120,120,175);
static constexpr ImU32 C_YELLOW       = IM_COL32(247,209,10,255);
static constexpr ImU32 C_TEXT         = IM_COL32(229,229,229,255);

static bool groupHasPage(const NavGroup& grp, int page){
    for (const auto& it : grp.items)
        if (it.page == page) return true;
    return false;
}

static bool drawRailButton(const char* id, const char* label, bool selected, float indent){
    const float width = ImGui::GetContentRegionAvail().x;
    if (width <= 2.f) return false;

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, {width, BTN_H});
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 fillCol   = hovered && !selected ? C_NAV_HOVER : C_NAV_FILL;
    ImU32 borderCol = selected ? C_YELLOW : C_NAV_BORDER;
    ImU32 textCol   = selected ? C_YELLOW : C_TEXT;

    dl->AddRectFilled(pos, {pos.x + width, pos.y + BTN_H}, fillCol);
    dl->AddRect(pos, {pos.x + width, pos.y + BTN_H}, borderCol);
    dl->AddText({pos.x + 10.f + indent, pos.y + (BTN_H - ImGui::GetFontSize()) * 0.5f}, textCol, label);

    ImGui::Dummy({0.f, BTN_GAP});
    return clicked;
}

// ─── Main Render ──────────────────────────────────────────────────────────
void Render(){
    StyleAPB();
    if(!s_configLoaded) loadConfig();
    ImGuiIO& io   = ImGui::GetIO();
    const float W = io.DisplaySize.x;
    const float H = io.DisplaySize.y;
    if (W <= 10.f || H <= 10.f) return;

    const float contentX = RAIL_W + 8.f;
    const float contentY = TITLE_H + 8.f;
    const float contentW = std::max(140.f, W - contentX - 10.f);
    const float contentH = std::max(80.f, H - contentY - BOTTOM_H - 8.f);
    const float bottomX  = contentX;
    const float bottomY  = H - BOTTOM_H;
    const float bottomW  = contentW;
    const float railX    = RAIL_MARGIN;
    const float railY    = TITLE_H + 8.f;
    const float railW    = RAIL_W - RAIL_MARGIN * 2.f;
    const float railH    = std::max(80.f, bottomY - railY - 8.f);

    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilled({0.f,0.f}, {W,H}, C_APP_BG);
    bg->AddRectFilled({0.f,0.f}, {W,TITLE_H}, C_TITLE_BG);
    bg->AddLine({0.f, TITLE_H - 1.f}, {W, TITLE_H - 1.f}, C_FRAME_BORDER);
    bg->AddRectFilled({0.f, TITLE_H}, {RAIL_W, H}, C_SIDE_BG);
    bg->AddLine({RAIL_W - 1.f, TITLE_H}, {RAIL_W - 1.f, H}, C_FRAME_BORDER);

    bg->AddRectFilled({contentX, contentY}, {contentX + contentW, contentY + contentH}, C_PANEL_BG);
    bg->AddRect({contentX, contentY}, {contentX + contentW, contentY + contentH}, C_FRAME_BORDER);
    bg->AddRectFilled({bottomX, bottomY}, {bottomX + bottomW, H}, C_BOTTOM_BG);
    bg->AddRect({bottomX, bottomY}, {bottomX + bottomW, H}, C_FRAME_BORDER);

    // ── Title bar (text + close button) ─────────────────────────────────
    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize({W, TITLE_H});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0,0,0,0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::Begin("##titlebar", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);

    {
        char title[160];
        snprintf(title, sizeof(title), "Options - %s", pageCategory[s_current]);
        float titleFs = ImGui::GetFontSize() * 2.05f;
        bg->AddText(ImGui::GetFont(), titleFs, {16.f, (TITLE_H - titleFs) * 0.5f - 1.f}, C_TEXT, title);
    }

    ImGui::SetCursorPos({W - 40.f, 8.f});
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.22f, 0.22f, 0.22f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.30f, 0.30f, 0.30f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.35f, 0.35f, 0.35f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_Border,        {0.55f, 0.55f, 0.55f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_Text,          {0.92f, 0.92f, 0.92f, 1.f});
    if (ImGui::Button("X##close", {28.f, 28.f})) PostQuitMessage(0);
    ImGui::PopStyleColor(5);

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // ── Left rail ────────────────────────────────────────────────────────
    ImGui::SetNextWindowPos({railX, railY});
    ImGui::SetNextWindowSize({railW, railH});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0,0,0,0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.f, 0.f});
    ImGui::Begin("##rail", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);

    for (int gi = 0; gi < (int)s_nav.size(); ++gi){
        auto& grp = s_nav[gi];
        bool grpActive = groupHasPage(grp, s_current);

        ImGui::PushID(gi);
        if (drawRailButton("##grp", grp.header, grpActive, 0.f)) {
            for (auto& g2 : s_nav) g2.open = false;
            grp.open = true;
            if (!grp.items.empty()) s_current = grp.items[0].page;
        }

        if (grp.open) {
            for (int ii = 0; ii < (int)grp.items.size(); ++ii){
                const auto& it = grp.items[ii];
                bool sel = (it.page == s_current);
                ImGui::PushID(ii);
                if (drawRailButton("##sub", it.label, sel, 14.f))
                    s_current = it.page;
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();

    // ── Content panel ────────────────────────────────────────────────────
    ImGui::SetNextWindowPos({contentX + 1.f, contentY + 1.f});
    ImGui::SetNextWindowSize({contentW - 2.f, contentH - 2.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.f, 12.f});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0,0,0,0});
    ImGui::Begin("##content", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);
    drawPage();
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // ── Bottom action bar ────────────────────────────────────────────────
    ImGui::SetNextWindowPos({bottomX + 1.f, bottomY + 4.f});
    ImGui::SetNextWindowSize({bottomW - 2.f, BOTTOM_H - 6.f});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0,0,0,0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::Begin("##bottom", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);

    {
        const float gap  = 8.f;
        const float side = 6.f;
        const float btnH = BOTTOM_H - 14.f;
        const float btnW = 110.f;
        const bool canCancel = selectedToolCanCancel();
        const bool canStart = selectedToolCanStart();
        ImGui::SetCursorPos({side, 7.f});
        ImGui::PushStyleColor(ImGuiCol_Text, s_configStatusOk ? ImVec4(Col::SUBTEXT) : ImVec4(Col::RED));
        ImGui::TextUnformatted(s_configStatus.c_str());
        ImGui::PopStyleColor();
        ImGui::SetCursorPos({bottomW - side - btnW * 2.f - gap, 1.f});

        auto apbButton = [&](const char* label) -> bool {
            ImGui::PushStyleColor(ImGuiCol_Button,        {0.24f, 0.24f, 0.24f, 1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.31f, 0.31f, 0.31f, 1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.36f, 0.36f, 0.36f, 1.f});
            ImGui::PushStyleColor(ImGuiCol_Border,        {0.45f, 0.45f, 0.45f, 1.f});
            ImGui::PushStyleColor(ImGuiCol_Text,          {0.92f, 0.92f, 0.92f, 1.f});
            bool pressed = ImGui::Button(label, {btnW, btnH});
            ImGui::PopStyleColor(5);
            return pressed;
        };

        if (!canCancel) ImGui::BeginDisabled();
        if (apbButton("Cancel")) cancelSelectedTool();
        if (!canCancel) ImGui::EndDisabled();
        ImGui::SameLine(0.f, gap);
        if (!canStart) ImGui::BeginDisabled();
        if (apbButton("OK")){
            saveConfig();
            startSelectedTool();
        }
        if (!canStart) ImGui::EndDisabled();
    }

    ImGui::End();
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor();
}

} // namespace apb::gui
