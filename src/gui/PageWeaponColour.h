#pragma once
#include "App.h"
#include "backend/WeaponColour.h"
#include "backend/GradientMaker.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace apb::gui {

struct PageWeaponColour {
    struct ColourRuleState {
        bool enabled = false;
        int modeIdx = 2; // 0=SOLID, 1=Stepped, 2=Smooth, 3=Triple Gradient
        float solid[3] = {1.f,1.f,1.f};
        float stepped[6][3] = {
            {1.f,1.f,1.f},{1.f,1.f,1.f},{1.f,1.f,1.f},
            {1.f,1.f,1.f},{1.f,1.f,1.f},{1.f,1.f,1.f}
        };
        float smoothStart[3] = {1.f,1.f,1.f};
        float smoothEnd[3]   = {1.f,1.f,1.f};
        float tripleStart[3] = {1.f,1.f,1.f};
        float tripleMid[3]   = {1.f,1.f,1.f};
        float tripleEnd[3]   = {1.f,1.f,1.f};
    };

    struct CategoryState : ColourRuleState {};

    struct SpecificWeaponState : ColourRuleState {
        std::string fullKey;
        std::string categoryToken;
        std::string itemToken;
        std::string displayName;
        std::string searchText;
    };

    char filePath[MAX_PATH] = {};
    char inventoryIntPath[MAX_PATH] = {};
    char weaponSearch[256] = {};
    int fontIdx = 0;
    int modeIdx = 2; // legacy config compatibility
    float singleCol[3] = {1.f,1.f,1.f}; // legacy config compatibility
    float gradStart[3] = {1.f,1.f,1.f}; // legacy config compatibility
    float gradEnd[3]   = {1.f,1.f,1.f}; // legacy config compatibility
    char ignoreList[8192] = {};
    char presetName[128] = "Custom";
    int presetModeIdx = 2;
    float presetSolid[3] = {1.f,1.f,1.f};
    float presetStepped[6][3] = {
        {1.f,1.f,1.f},{1.f,1.f,1.f},{1.f,1.f,1.f},
        {1.f,1.f,1.f},{1.f,1.f,1.f},{1.f,1.f,1.f}
    };
    float presetSmoothStart[3] = {1.f,1.f,1.f};
    float presetSmoothEnd[3]   = {1.f,1.f,1.f};
    float presetTripleStart[3] = {1.f,1.f,1.f};
    float presetTripleMid[3]   = {1.f,1.f,1.f};
    float presetTripleEnd[3]   = {1.f,1.f,1.f};

    ThreadLog log;
    std::atomic<bool> running{false};
    std::atomic<bool> cancelRequested{false};
    std::string lastOut;
    std::string inventoryScanStatus = "Scan InventoryItemTypes.INT to load specific weapons.";
    bool inventoryScanPending = false;
    bool autoDetectPending = true;

    std::vector<std::string> fonts;
    std::vector<GunTypeDefinition> gunTypes;
    std::vector<CategoryState> categories;
    std::vector<SpecificWeaponState> specificWeapons;

    static constexpr const char* MODES[] = {"SOLID","Stepped","Smooth","Triple Gradient"};

    PageWeaponColour(){
        fonts = availableFonts();
        gunTypes = weaponGunTypes();
        categories.resize(gunTypes.size());
        if(!categories.empty())
            categories[0].enabled = true;
        for(auto& st : categories)
            applyDefaultPalette(st);

        autoDetectDefaultPaths();
    }

    static RGB rgbFromFloats(const float c[3]){
        return {c[0], c[1], c[2]};
    }

    static void copyColor(float dst[3], const float src[3]){
        dst[0]=src[0]; dst[1]=src[1]; dst[2]=src[2];
    }

    static std::string lowerCopy(std::string value){
        for(char& ch : value)
            ch = (char)std::tolower((unsigned char)ch);
        return value;
    }

    static ColourMode modeFromIndex(int modeIdx){
        return modeIdx == 0 ? ColourMode::SOLID :
               modeIdx == 1 ? ColourMode::STEPPED :
               modeIdx == 2 ? ColourMode::SMOOTH :
                              ColourMode::TRIPLE;
    }

    static bool matchesSearch(const SpecificWeaponState& st, const char* filter){
        if(!filter || !*filter) return true;
        return st.searchText.find(lowerCopy(filter)) != std::string::npos;
    }

    void autoDetectDefaultPaths(){
        if(!filePath[0]){
            const std::string detectedGer = DetectApbIntFile("InventoryItemTypes.GER");
            if(!detectedGer.empty())
                std::snprintf(filePath, sizeof(filePath), "%s", detectedGer.c_str());
        }
        if(!inventoryIntPath[0]){
            const std::string detectedInt = DetectApbIntFile("InventoryItemTypes.INT");
            if(!detectedInt.empty()){
                std::snprintf(inventoryIntPath, sizeof(inventoryIntPath), "%s", detectedInt.c_str());
                inventoryScanPending = true;
            }
        }
    }

    void applyDefaultPalette(ColourRuleState& st){
        RGB s = rgbFromFloats(st.smoothStart);
        RGB e = rgbFromFloats(st.smoothEnd);
        for(int i = 0; i < 6; ++i){
            RGB c = lerpRGB(s, e, double(i) / 5.0);
            st.stepped[i][0] = (float)c.r;
            st.stepped[i][1] = (float)c.g;
            st.stepped[i][2] = (float)c.b;
        }
    }

    void drawModeCombo(const char* id, ColourRuleState& st){
        ImGui::SetNextItemWidth(-1.f);
        if(ImGui::BeginCombo(id, MODES[st.modeIdx])){
            for(int m = 0; m < 4; ++m){
                if(ImGui::Selectable(MODES[m], st.modeIdx == m))
                    st.modeIdx = m;
                if(st.modeIdx == m) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    void drawColourControls(const char* prefix, int idx, ColourRuleState& st){
        ImGui::PushID(prefix);
        ImGui::PushID(idx);

        if(st.modeIdx == 0){
            ColorPickerButton("##solid", st.solid);
        } else if(st.modeIdx == 1){
            for(int i = 0; i < 6; ++i){
                char id[16]; std::snprintf(id, sizeof(id), "##step%d", i);
                ColorPickerButton(id, st.stepped[i], 22.f, 20.f);
                if(i < 5) ImGui::SameLine();
            }
        } else if(st.modeIdx == 2){
            ImGui::TextUnformatted("Start"); ImGui::SameLine();
            ColorPickerButton("##smoothA", st.smoothStart);
            ImGui::SameLine();
            ImGui::TextUnformatted("End"); ImGui::SameLine();
            ColorPickerButton("##smoothB", st.smoothEnd);
        } else {
            ImGui::TextUnformatted("1"); ImGui::SameLine();
            ColorPickerButton("##tripleA", st.tripleStart);
            ImGui::SameLine();
            ImGui::TextUnformatted("2"); ImGui::SameLine();
            ColorPickerButton("##tripleB", st.tripleMid);
            ImGui::SameLine();
            ImGui::TextUnformatted("3"); ImGui::SameLine();
            ColorPickerButton("##tripleC", st.tripleEnd);
        }

        ImGui::PopID();
        ImGui::PopID();
    }

    void drawPresetColourControls(){
        if(presetModeIdx == 0){
            ColorPickerButton("##presetSolid", presetSolid);
        } else if(presetModeIdx == 1){
            for(int i = 0; i < 6; ++i){
                char id[20]; std::snprintf(id, sizeof(id), "##presetStep%d", i);
                ColorPickerButton(id, presetStepped[i], 24.f, 20.f);
                if(i < 5) ImGui::SameLine();
            }
        } else if(presetModeIdx == 2){
            ImGui::TextUnformatted("Start"); ImGui::SameLine();
            ColorPickerButton("##presetSmoothStart", presetSmoothStart);
            ImGui::SameLine();
            ImGui::TextUnformatted("End"); ImGui::SameLine();
            ColorPickerButton("##presetSmoothEnd", presetSmoothEnd);
        } else {
            ImGui::TextUnformatted("1"); ImGui::SameLine();
            ColorPickerButton("##presetTripleStart", presetTripleStart);
            ImGui::SameLine();
            ImGui::TextUnformatted("2"); ImGui::SameLine();
            ColorPickerButton("##presetTripleMid", presetTripleMid);
            ImGui::SameLine();
            ImGui::TextUnformatted("3"); ImGui::SameLine();
            ColorPickerButton("##presetTripleEnd", presetTripleEnd);
        }
    }

    void applyPresetToRule(ColourRuleState& st){
        st.modeIdx = presetModeIdx;
        copyColor(st.solid, presetSolid);
        for(int i = 0; i < 6; ++i)
            copyColor(st.stepped[i], presetStepped[i]);
        copyColor(st.smoothStart, presetSmoothStart);
        copyColor(st.smoothEnd, presetSmoothEnd);
        copyColor(st.tripleStart, presetTripleStart);
        copyColor(st.tripleMid, presetTripleMid);
        copyColor(st.tripleEnd, presetTripleEnd);
    }

    void scanSpecificWeapons(){
        const std::string path = inventoryIntPath;
        if(path.empty()){
            inventoryScanStatus = "Select InventoryItemTypes.INT first.";
            specificWeapons.clear();
            return;
        }

        try{
            std::map<std::string, SpecificWeaponState> previous;
            for(const auto& item : specificWeapons)
                previous[lowerCopy(item.fullKey)] = item;

            const auto scanned = scanInventoryWeapons(path);
            specificWeapons.clear();
            specificWeapons.reserve(scanned.size());

            for(const auto& item : scanned){
                SpecificWeaponState st;
                auto it = previous.find(lowerCopy(item.fullKey));
                if(it != previous.end()){
                    st = it->second;
                } else {
                    applyDefaultPalette(st);
                }

                st.fullKey = item.fullKey;
                st.categoryToken = item.categoryToken;
                st.itemToken = item.itemToken;
                st.displayName = item.displayName;
                st.searchText = lowerCopy(
                    item.displayName + " " + item.categoryToken + " " + item.itemToken + " " + item.fullKey);
                specificWeapons.push_back(std::move(st));
            }

            char buf[256];
            std::snprintf(buf, sizeof(buf), "Loaded %d weapons from InventoryItemTypes.INT.",
                (int)specificWeapons.size());
            inventoryScanStatus = buf;
        } catch(const std::exception& e){
            specificWeapons.clear();
            inventoryScanStatus = std::string("Scan failed: ") + e.what();
        }
    }

    void drawPresetConfig(){
        SectionLabel("Preset Config");
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::ITEM_BG);
        ImGui::BeginChild("##wcPresetConfig", {0.f, 146.f}, true);

        ImGui::Text("Preset Name:"); ImGui::SameLine(112.f);
        ImGui::SetNextItemWidth(220.f);
        ImGui::InputText("##wcPresetName", presetName, sizeof(presetName));
        ImGui::SameLine();
        ImGui::Text("Mode:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(170.f);
        if(ImGui::BeginCombo("##wcPresetMode", MODES[presetModeIdx])){
            for(int m=0;m<4;++m){
                if(ImGui::Selectable(MODES[m], presetModeIdx==m))
                    presetModeIdx = m;
                if(presetModeIdx==m) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();
        ImGui::Text("Colours:"); ImGui::SameLine(112.f);
        drawPresetColourControls();

        ImGui::Spacing();
        if(ImGui::Button("Apply to Enabled Categories##wcPreset", {190.f, 28.f})){
            for(auto& st : categories)
                if(st.enabled) applyPresetToRule(st);
        }
        ImGui::SameLine();
        if(ImGui::Button("Apply to All Categories##wcPreset", {164.f, 28.f})){
            for(auto& st : categories)
                applyPresetToRule(st);
        }
        ImGui::SameLine();
        if(ImGui::Button("Apply to Selected Weapons##wcPreset", {182.f, 28.f})){
            for(auto& st : specificWeapons)
                if(st.enabled) applyPresetToRule(st);
        }
        ImGui::Spacing();
        ImGui::TextColored(Col::SUBTEXT, "Saved with app config when OK is pressed.");

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    GunTypeColourSettings buildCategorySettings(int idx) const {
        const auto& def = gunTypes[idx];
        const auto& st = categories[idx];
        GunTypeColourSettings out;
        out.categoryToken = def.categoryToken;
        out.enabled = st.enabled;
        out.mode = modeFromIndex(st.modeIdx);
        out.solid = rgbFromFloats(st.solid);
        out.smoothStart = rgbFromFloats(st.smoothStart);
        out.smoothEnd = rgbFromFloats(st.smoothEnd);
        out.tripleStart = rgbFromFloats(st.tripleStart);
        out.tripleMiddle = rgbFromFloats(st.tripleMid);
        out.tripleEnd = rgbFromFloats(st.tripleEnd);
        out.stepped.reserve(6);
        for(int i = 0; i < 6; ++i)
            out.stepped.push_back(rgbFromFloats(st.stepped[i]));
        return out;
    }

    GunTypeColourSettings buildSpecificSettings(const SpecificWeaponState& st) const {
        GunTypeColourSettings out;
        out.categoryToken = st.categoryToken;
        out.fullKey = st.fullKey;
        out.enabled = st.enabled;
        out.mode = modeFromIndex(st.modeIdx);
        out.solid = rgbFromFloats(st.solid);
        out.smoothStart = rgbFromFloats(st.smoothStart);
        out.smoothEnd = rgbFromFloats(st.smoothEnd);
        out.tripleStart = rgbFromFloats(st.tripleStart);
        out.tripleMiddle = rgbFromFloats(st.tripleMid);
        out.tripleEnd = rgbFromFloats(st.tripleEnd);
        out.stepped.reserve(6);
        for(int i = 0; i < 6; ++i)
            out.stepped.push_back(rgbFromFloats(st.stepped[i]));
        return out;
    }

    std::vector<std::string> ignoredKeys() const {
        std::vector<std::string> out;
        std::istringstream ss(ignoreList);
        std::string line;
        while(std::getline(ss,line)){
            if(!line.empty() && line.back()=='\r') line.pop_back();
            out.push_back(line);
        }
        return out;
    }

    bool isActionRunning() const { return running.load(); }
    bool canStartAction() const { return !running.load() && filePath[0]; }

    void startAction(){
        if(canStartAction()) runProcess();
    }

    void cancelAction(){
        if(!running.load()) return;
        cancelRequested = true;
        log.append("Cancelling...");
    }

    void drawSpecificWeaponsSection(){
        SectionLabel("Specific Weapon");

        ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::ITEM_BG);
        ImGui::BeginChild("##wcSpecificConfig", {0.f, 102.f}, true);

        ImGui::Text("Inventory INT:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(-156.f);
        ImGui::InputText("##wcInventoryInt", inventoryIntPath, sizeof(inventoryIntPath));
        ImGui::SameLine();
        if(ImGui::Button("Browse##wcInventoryInt")){
            std::string s;
            if(BrowseFile(s, "INT Files\0*.int;*.INT\0All Files\0*.*\0\0"))
                std::snprintf(inventoryIntPath, sizeof(inventoryIntPath), "%s", s.c_str());
        }
        ImGui::SameLine();
        if(ImGui::Button("Scan##wcInventoryInt"))
            scanSpecificWeapons();

        ImGui::TextColored(Col::SUBTEXT, "%s", inventoryScanStatus.c_str());
        ImGui::TextColored(Col::SUBTEXT, "Specific weapon rules override category rules when both match.");

        ImGui::Text("Filter:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(260.f);
        ImGui::InputText("##wcWeaponSearch", weaponSearch, sizeof(weaponSearch));
        ImGui::SameLine();
        if(ImGui::SmallButton("All Visible##wcSpecific")){
            for(auto& st : specificWeapons)
                if(matchesSearch(st, weaponSearch)) st.enabled = true;
        }
        ImGui::SameLine();
        if(ImGui::SmallButton("None Visible##wcSpecific")){
            for(auto& st : specificWeapons)
                if(matchesSearch(st, weaponSearch)) st.enabled = false;
        }
        ImGui::SameLine();
        if(ImGui::SmallButton("Clear Search##wcSpecific"))
            weaponSearch[0] = '\0';

        ImGui::EndChild();
        ImGui::PopStyleColor();

        if(specificWeapons.empty()){
            ImGui::TextColored(Col::SUBTEXT, "No specific weapon list loaded.");
            return;
        }

        int visibleCount = 0;
        for(const auto& st : specificWeapons)
            if(matchesSearch(st, weaponSearch)) ++visibleCount;
        ImGui::TextColored(Col::SUBTEXT, "%d weapons loaded, %d visible.", (int)specificWeapons.size(), visibleCount);

        if(ImGui::BeginTable("##wcSpecificWeapons", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
            {-1.f, 320.f}))
        {
            ImGui::TableSetupColumn("Apply", ImGuiTableColumnFlags_WidthFixed, 52.f);
            ImGui::TableSetupColumn("Weapon", ImGuiTableColumnFlags_WidthFixed, 250.f);
            ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 110.f);
            ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 150.f);
            ImGui::TableSetupColumn("Colours", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for(int i = 0; i < (int)specificWeapons.size(); ++i){
                auto& st = specificWeapons[i];
                if(!matchesSearch(st, weaponSearch))
                    continue;

                ImGui::PushID(i);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Checkbox("##enabled", &st.enabled);

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(st.displayName.c_str());
                if(ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", st.fullKey.c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(st.categoryToken.c_str());

                ImGui::TableSetColumnIndex(3);
                drawModeCombo("##mode", st);

                ImGui::TableSetColumnIndex(4);
                drawColourControls("specific", i, st);

                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }

    void draw(){
        if(autoDetectPending){
            autoDetectPending = false;
            autoDetectDefaultPaths();
        }

        if(inventoryScanPending && inventoryIntPath[0]){
            inventoryScanPending = false;
            scanSpecificWeapons();
        }

        ImGui::BeginChild("##wc",{0,0},false);
        SectionLabel("Weapon Colour - InventoryItemTypes.GER");

        ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::ITEM_BG);
        ImGui::BeginChild("##wcFileConfig", {0.f, 74.f}, true);
        ImGui::Text("GER File:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(-80);
        ImGui::InputText("##wcpath", filePath, sizeof(filePath));
        ImGui::SameLine();
        if(ImGui::Button("Browse##wc")){
            std::string s;
            if(BrowseFile(s,"GER Files\0*.ger;*.GER\0All Files\0*.*\0\0"))
                std::snprintf(filePath, sizeof(filePath), "%s", s.c_str());
        }

        ImGui::Text("Font:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(360);
        if(fontIdx < 0 || fontIdx >= (int)fonts.size()) fontIdx = 0;
        if(ImGui::BeginCombo("##wcfont", fonts.empty() ? "None" : fonts[fontIdx].c_str())){
            for(int i = 0; i < (int)fonts.size(); ++i){
                if(ImGui::Selectable(fonts[i].c_str(), fontIdx == i)) fontIdx = i;
                if(fontIdx == i) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if(ImGui::SmallButton("All##wcCats")){
            for(auto& st : categories) st.enabled = true;
        }
        ImGui::SameLine();
        if(ImGui::SmallButton("None##wcCats")){
            for(auto& st : categories) st.enabled = false;
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        drawPresetConfig();

        if(ImGui::BeginTabBar("##wcModeTabs")){
            if(ImGui::BeginTabItem("Weapon Categories")){
                SectionLabel("Weapon Categories");

                if(ImGui::BeginTable("##wcCategories", 4,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                    {-1.f, 578.f}))
                {
                    ImGui::TableSetupColumn("Apply", ImGuiTableColumnFlags_WidthFixed, 52.f);
                    ImGui::TableSetupColumn("Gun type", ImGuiTableColumnFlags_WidthFixed, 170.f);
                    ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 150.f);
                    ImGui::TableSetupColumn("Colours", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    for(int i = 0; i < (int)gunTypes.size(); ++i){
                        auto& st = categories[i];
                        ImGui::PushID(i);
                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        ImGui::Checkbox("##enabled", &st.enabled);

                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(gunTypes[i].label.c_str());
                        if(ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", gunTypes[i].keyPrefix.c_str());

                        ImGui::TableSetColumnIndex(2);
                        drawModeCombo("##mode", st);

                        ImGui::TableSetColumnIndex(3);
                        drawColourControls("category", i, st);

                        ImGui::PopID();
                    }

                    ImGui::EndTable();
                }

                ImGui::EndTabItem();
            }

            if(ImGui::BeginTabItem("Specific Weapon")){
                drawSpecificWeaponsSection();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::Spacing();
        if(ImGui::CollapsingHeader("Ignore List", ImGuiTreeNodeFlags_DefaultOpen)){
            ImGui::TextColored(Col::SUBTEXT, "One key, key prefix, pasted line, or substring per line.");
            ImGui::InputTextMultiline("##wcIgnoreList", ignoreList, sizeof(ignoreList), {-1.f, 78.f});
        }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        bool busy = running.load();
        if(busy) ImGui::BeginDisabled();
        if(RunButton("Run##wc")) startAction();
        if(busy) ImGui::EndDisabled();
        ImGui::SameLine();
        if(!lastOut.empty() && ImGui::Button("Open Output", {110,32})) OpenInExplorer(lastOut);
        if(busy){ ImGui::SameLine(); ImGui::TextColored(Col::YELLOW, "Running..."); }

        ImGui::Spacing();
        std::string logText = log.get();
        ImGui::InputTextMultiline("##wclog", (char*)logText.c_str(), logText.size() + 1, {-1,0}, ImGuiInputTextFlags_ReadOnly);
        ImGui::EndChild();
    }

    void runProcess(){
        bool anyEnabled = false;
        std::vector<GunTypeColourSettings> settings;
        settings.reserve(categories.size() + specificWeapons.size());
        for(int i = 0; i < (int)categories.size(); ++i){
            settings.push_back(buildCategorySettings(i));
            anyEnabled = anyEnabled || categories[i].enabled;
        }
        for(const auto& st : specificWeapons){
            settings.push_back(buildSpecificSettings(st));
            anyEnabled = anyEnabled || st.enabled;
        }

        if(!anyEnabled){
            log.append("No gun types or specific weapons selected.");
            return;
        }

        log.clear();
        cancelRequested = false;
        running = true;
        lastOut.clear();

        std::string path = filePath;
        std::string font = (fontIdx >= 0 && fontIdx < (int)fonts.size()) ? fonts[fontIdx] : "None";
        std::vector<std::string> ignored = ignoredKeys();

        std::thread([this,path,settings,font,ignored](){
            try{
                auto r = applyColourToGerFile(path, settings, font, ignored,
                    [this](const std::string& s){ log.append(s); },
                    &cancelRequested);
                if(r.cancelled || cancelRequested.load()){
                    log.append("Cancelled.");
                } else {
                    lastOut = r.outputPath;
                    char buf[320];
                    std::snprintf(buf, sizeof(buf), "Done: %d recoloured, %d skipped -> %s",
                        r.newlyColoured, r.skippedRules, r.outputPath.c_str());
                    log.append(buf);
                }
            } catch(const std::exception& e){
                log.append(std::string("Error: ") + e.what());
            }
            cancelRequested = false;
            running = false;
        }).detach();
    }
};

} // namespace apb::gui
