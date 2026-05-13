#pragma once
#include "App.h"
#include "backend/WeaponColour.h"
#include "backend/GradientMaker.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <numeric>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace apb::gui {

struct PageWeaponColour {
    struct ColourRuleState {
        bool enabled = false;
        int fontIdx = 0;
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
    char shopGerPath[MAX_PATH] = {};
    char customOutputPath[MAX_PATH] = {};
    char weaponSearch[256] = {};
    int fontIdx = 0; // legacy shared default for config migration and new rule defaults
    int specificSortIdx = 1;
    int specificCategoryFilterIdx = 0;
    int modeIdx = 2; // legacy config compatibility
    float singleCol[3] = {1.f,1.f,1.f}; // legacy config compatibility
    float gradStart[3] = {1.f,1.f,1.f}; // legacy config compatibility
    float gradEnd[3]   = {1.f,1.f,1.f}; // legacy config compatibility
    char ignoreList[8192] = {};
    int quickPresetIdx = 0; // 0=Custom, 1=Writchcraft, 2=Spellbound
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
    bool categoryOrderPending = true;

    std::vector<std::string> fonts;
    std::vector<GunTypeDefinition> gunTypes;
    std::vector<CategoryState> categories;
    std::vector<SpecificWeaponState> specificWeapons;

    static constexpr const char* MODES[] = {"SOLID","Stepped","Smooth","Triple Gradient"};
    static constexpr const char* SPECIFIC_SORTS[] = {"APB Class Order","Weapon Name"};
    static constexpr const char* PRESET_NAMES[] = {"Custom","Writchcraft","Spellbound"};

    PageWeaponColour(){
        fonts = availableFonts();
        gunTypes = weaponGunTypes();
        categories.resize(gunTypes.size());
        if(!categories.empty())
            categories[0].enabled = true;
        for(auto& st : categories){
            applyDefaultPalette(st);
            st.fontIdx = fontIdx;
        }

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

    std::string computedOutputPath() const{
        return customOutputPath[0] ? std::string(customOutputPath) : std::string{};
    }

    std::string resolvedOutputPath() const{
        if(!inventoryIntPath[0]) return {};
        namespace fs = std::filesystem;
        const std::string dir = customOutputPath[0] ? std::string(customOutputPath) : DownloadsDir();
        if(dir.empty()) return {};
        return (fs::path(dir) / fs::path(inventoryIntPath).filename()).string();
    }

    int categoryOrder(const std::string& categoryToken) const{
        for(int i = 0; i < (int)gunTypes.size(); ++i){
            if(gunTypes[i].categoryToken == categoryToken)
                return i;
        }
        return (int)gunTypes.size();
    }

    const char* categoryFilterLabel(int idx) const{
        if(idx <= 0) return "All Classes";
        const int gunIdx = idx - 1;
        if(gunIdx >= 0 && gunIdx < (int)gunTypes.size())
            return gunTypes[gunIdx].label.c_str();
        return "All Classes";
    }

    bool matchesCategoryFilter(const SpecificWeaponState& st) const{
        if(specificCategoryFilterIdx <= 0) return true;
        const int gunIdx = specificCategoryFilterIdx - 1;
        return gunIdx >= 0 && gunIdx < (int)gunTypes.size()
            && st.categoryToken == gunTypes[gunIdx].categoryToken;
    }

    bool isSpecificVisible(const SpecificWeaponState& st) const{
        return matchesCategoryFilter(st) && matchesSearch(st, weaponSearch);
    }

    void sortSpecificWeapons(){
        std::stable_sort(specificWeapons.begin(), specificWeapons.end(),
            [this](const SpecificWeaponState& a, const SpecificWeaponState& b){
                if(specificSortIdx == 1){
                    if(a.displayName != b.displayName)
                        return a.displayName < b.displayName;
                    return a.fullKey < b.fullKey;
                }

                const int aOrder = categoryOrder(a.categoryToken);
                const int bOrder = categoryOrder(b.categoryToken);
                if(aOrder != bOrder)
                    return aOrder < bOrder;
                if(a.displayName != b.displayName)
                    return a.displayName < b.displayName;
                return a.fullKey < b.fullKey;
            });
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

    const char* fontLabel(int idx) const{
        if(fonts.empty())
            return "None";
        idx = std::clamp(idx, 0, (int)fonts.size() - 1);
        return fonts[idx].c_str();
    }

    void drawFontCombo(const char* id, int& selectedFontIdx){
        ImGui::SetNextItemWidth(-1.f);
        if(fonts.empty()){
            ImGui::BeginDisabled();
            char noneBuf[] = "None";
            ImGui::InputText(id, noneBuf, sizeof(noneBuf), ImGuiInputTextFlags_ReadOnly);
            ImGui::EndDisabled();
            return;
        }

        selectedFontIdx = std::clamp(selectedFontIdx, 0, (int)fonts.size() - 1);
        const char* preview = fonts[selectedFontIdx].c_str();
        if(ImGui::BeginCombo(id, preview)){
            for(int i = 0; i < (int)fonts.size(); ++i){
                if(ImGui::Selectable(fonts[i].c_str(), selectedFontIdx == i))
                    selectedFontIdx = i;
                if(selectedFontIdx == i) ImGui::SetItemDefaultFocus();
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
                    st.fontIdx = fontIdx;
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
            sortSpecificWeapons();
        } catch(const std::exception& e){
            specificWeapons.clear();
            inventoryScanStatus = std::string("Scan failed: ") + e.what();
        }
    }

    void loadCategoryOrder(){
        namespace fs = std::filesystem;
        std::string resolvedShopPath = shopGerPath[0] ? std::string(shopGerPath) : std::string{};
        if(resolvedShopPath.empty()){
            if(inventoryIntPath[0]){
                std::error_code ec;
                fs::path locDir = fs::path(inventoryIntPath).parent_path();
                for(const char* name : {"ShopUIFilters.GER","ShopUIFilters.INT","ShopUIFilters.ger","ShopUIFilters.int"}){
                    fs::path candidate = locDir / name;
                    if(fs::exists(candidate, ec)){ resolvedShopPath = candidate.string(); break; }
                }
                if(resolvedShopPath.empty()){
                    fs::path gerDir = locDir.parent_path() / "GER";
                    fs::path candidate = gerDir / "ShopUIFilters.GER";
                    if(fs::exists(candidate, ec)) resolvedShopPath = candidate.string();
                }
            }
            if(resolvedShopPath.empty()) resolvedShopPath = DetectApbLocalizationFile("GER", "ShopUIFilters.GER");
            if(resolvedShopPath.empty()) resolvedShopPath = DetectApbIntFile("ShopUIFilters.GER");
        }

        std::map<std::string,int> orderMap;
        if(!resolvedShopPath.empty())
            orderMap = apb::parseShopUIFilterOrder(resolvedShopPath);
        if(orderMap.empty())
            orderMap = apb::defaultShopUIFilterOrder();

        const int n = (int)gunTypes.size();
        std::vector<int> idx(n);
        std::iota(idx.begin(), idx.end(), 0);
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b){
            auto ia = orderMap.find(gunTypes[a].categoryToken);
            auto ib = orderMap.find(gunTypes[b].categoryToken);
            int va = (ia != orderMap.end()) ? ia->second : n + a;
            int vb = (ib != orderMap.end()) ? ib->second : n + b;
            return va < vb;
        });

        std::vector<GunTypeDefinition> newTypes;
        std::vector<CategoryState> newCats;
        newTypes.reserve(n);
        newCats.reserve(n);
        for(int i : idx){
            newTypes.push_back(std::move(gunTypes[i]));
            newCats.push_back(std::move(categories[i]));
        }
        gunTypes    = std::move(newTypes);
        categories  = std::move(newCats);
        specificCategoryFilterIdx = 0;
    }

    void loadNamedPresetColours(){
        if(quickPresetIdx == 0) return;
        const RGB start = (quickPresetIdx == 1) ? apb::WRITCH_START() : apb::SPELL_START();
        const RGB end   = (quickPresetIdx == 1) ? apb::WRITCH_END()   : apb::SPELL_END();
        presetSmoothStart[0]=(float)start.r; presetSmoothStart[1]=(float)start.g; presetSmoothStart[2]=(float)start.b;
        presetSmoothEnd[0]  =(float)end.r;   presetSmoothEnd[1]  =(float)end.g;   presetSmoothEnd[2]  =(float)end.b;
        const RGB mid = apb::lerpRGB(start, end, 0.5);
        presetTripleStart[0]=(float)start.r; presetTripleStart[1]=(float)start.g; presetTripleStart[2]=(float)start.b;
        presetTripleMid[0]  =(float)mid.r;   presetTripleMid[1]  =(float)mid.g;   presetTripleMid[2]  =(float)mid.b;
        presetTripleEnd[0]  =(float)end.r;   presetTripleEnd[1]  =(float)end.g;   presetTripleEnd[2]  =(float)end.b;
        for(int i = 0; i < 6; ++i){
            const RGB c = apb::lerpRGB(start, end, double(i)/5.0);
            presetStepped[i][0]=(float)c.r; presetStepped[i][1]=(float)c.g; presetStepped[i][2]=(float)c.b;
        }
    }

    void applyNamedPresetToCategories(bool enabledOnly){
        const auto map = apb::presetByIndex(quickPresetIdx - 1);
        for(int i = 0; i < (int)categories.size(); ++i){
            auto& st = categories[i];
            if(enabledOnly && !st.enabled) continue;
            auto it = map.find(gunTypes[i].categoryToken);
            if(it == map.end()) continue;
            st.modeIdx  = 0;
            st.solid[0] = (float)it->second.r;
            st.solid[1] = (float)it->second.g;
            st.solid[2] = (float)it->second.b;
        }
    }

    void applyNamedPresetToWeapons(){
        const auto map = apb::presetByIndex(quickPresetIdx - 1);
        for(auto& st : specificWeapons){
            if(!st.enabled) continue;
            auto it = map.find(st.categoryToken);
            if(it == map.end()) continue;
            st.modeIdx  = 0;
            st.solid[0] = (float)it->second.r;
            st.solid[1] = (float)it->second.g;
            st.solid[2] = (float)it->second.b;
        }
    }

    void drawPresetConfig(){
        SectionLabel("Quick Preset");
        SectionNote("Build one palette here, then stamp it onto category rules or selected weapon overrides.");
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::ITEM_BG);
        ImGui::BeginChild("##wcPresetConfig", {0.f, 128.f}, true);

        if(BeginSectionTable("##wcPresetGrid", 96.f, 0.f)){
            BeginSectionRow("Preset");
            ImGui::SetNextItemWidth(200.f);
            if(ImGui::BeginCombo("##wcPresetSelect", PRESET_NAMES[quickPresetIdx])){
                for(int p = 0; p < 3; ++p){
                    if(ImGui::Selectable(PRESET_NAMES[p], quickPresetIdx == p)){
                        quickPresetIdx = p;
                        loadNamedPresetColours();
                    }
                    if(quickPresetIdx == p) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted("Mode");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(170.f);
            if(ImGui::BeginCombo("##wcPresetMode", MODES[presetModeIdx])){
                for(int m=0;m<4;++m){
                    if(ImGui::Selectable(MODES[m], presetModeIdx==m))
                        presetModeIdx = m;
                    if(presetModeIdx==m) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            BeginSectionRow("Colours");
            if(quickPresetIdx > 0 && presetModeIdx == 0){
                ImGui::TextColored(Col::SUBTEXT, "Per-category colours applied when stamped.");
            } else {
                drawPresetColourControls();
            }
            EndSectionTable();
        }

        ImGui::Spacing();
        const bool usePerCat = (quickPresetIdx > 0 && presetModeIdx == 0);
        if(ImGui::Button("Apply to Enabled Categories##wcPreset", {200.f, 28.f})){
            if(usePerCat){
                applyNamedPresetToCategories(true);
            } else {
                for(auto& st : categories)
                    if(st.enabled) applyPresetToRule(st);
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("Apply to All Categories##wcPreset", {176.f, 28.f})){
            if(usePerCat){
                applyNamedPresetToCategories(false);
            } else {
                for(auto& st : categories)
                    applyPresetToRule(st);
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("Apply to Selected Weapons##wcPreset", {190.f, 28.f})){
            if(usePerCat){
                applyNamedPresetToWeapons();
            } else {
                for(auto& st : specificWeapons)
                    if(st.enabled) applyPresetToRule(st);
            }
        }
        ImGui::TextColored(Col::SUBTEXT, "This preset is just a shortcut for stamping colour rules.");

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void drawCategorySection(){
        SectionLabel("Category Rules");
        SectionNote("Start here. Most setups only need category rules, with one palette per weapon class.");

        if(ImGui::BeginTable("##wcCategoryToolbar", 2,
            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings))
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(Col::SUBTEXT, "%d categories", (int)gunTypes.size());
            ImGui::TableSetColumnIndex(1);
            const float actionWidth = 110.f;
            const float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rightEdge - actionWidth * 2.f - 8.f));
            if(ImGui::SmallButton("Enable All##wcCats")){
                for(auto& st : categories) st.enabled = true;
            }
            ImGui::SameLine();
            if(ImGui::SmallButton("Clear All##wcCats")){
                for(auto& st : categories) st.enabled = false;
            }
            ImGui::EndTable();
        }

        ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::ITEM_BG);
        ImGui::BeginChild("##wcCategoriesPanel", {0.f, 404.f}, true);
        if(ImGui::BeginTable("##wcCategoriesSimple", 4,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_ScrollY,
            {-1.f, 0.f}))
        {
            ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 210.f);
            ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 160.f);
            ImGui::TableSetupColumn("Colours", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Font", ImGuiTableColumnFlags_WidthFixed, 210.f);

            for(int i = 0; i < (int)gunTypes.size(); ++i){
                auto& st = categories[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::Checkbox("##enabled", &st.enabled);
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextUnformatted(gunTypes[i].label.c_str());
                ImGui::TextColored(Col::SUBTEXT, "%s", gunTypes[i].categoryToken.c_str());
                ImGui::EndGroup();
                if(ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", gunTypes[i].keyPrefix.c_str());

                ImGui::TableSetColumnIndex(1);
                drawModeCombo("##mode", st);

                ImGui::TableSetColumnIndex(2);
                drawColourControls("category", i, st);

                ImGui::TableSetColumnIndex(3);
                drawFontCombo("##font", st.fontIdx);

                ImGui::PopID();
            }

            ImGui::EndTable();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void drawFileSettingsSection(){
        SectionLabel("File Settings");
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::ITEM_BG);
        ImGui::BeginChild("##wcFileConfig", {0.f, 140.f}, true);
        if(BeginSectionTable("##wcFileInventoryGrid", 126.f, 86.f)){
            BeginSectionRow("InventoryItemTypes");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##wcInventoryMain", inventoryIntPath, sizeof(inventoryIntPath));
            NextSectionAction();
            if(ImGui::Button("Browse##wcInventoryMain")){
                std::string s;
                if(BrowseFile(s, "All Files\0*.*\0\0")){
                    std::snprintf(inventoryIntPath, sizeof(inventoryIntPath), "%s", s.c_str());
                    categoryOrderPending = true;
                    inventoryScanPending = true;
                }
            }

            BeginSectionRow("ShopUIFilters");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##wcShopGer", shopGerPath, sizeof(shopGerPath));
            if(ImGui::IsItemHovered() && !shopGerPath[0])
                ImGui::SetTooltip("Auto-detected on load. Controls category sort order (F= values).");
            NextSectionAction();
            if(ImGui::Button("Browse##wcShopGer")){
                std::string s;
                if(BrowseFile(s, "All Files\0*.*\0\0")){
                    std::snprintf(shopGerPath, sizeof(shopGerPath), "%s", s.c_str());
                    categoryOrderPending = true;
                }
            }

            BeginSectionRow("Output Folder");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##wcOutputMain", customOutputPath, sizeof(customOutputPath));
            if(ImGui::IsItemHovered() && !customOutputPath[0])
                ImGui::SetTooltip("Blank = saves to Downloads");
            NextSectionAction();
            if(ImGui::Button("Browse##wcOutputMain")){
                std::string s;
                if(BrowseFolder(s))
                    std::snprintf(customOutputPath, sizeof(customOutputPath), "%s", s.c_str());
            }
            EndSectionTable();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void drawIgnoreRulesSection(){
        ImGui::Spacing();
        SectionLabel("Ignore Rules");
        if(ImGui::CollapsingHeader("Edit Ignore Rules", ImGuiTreeNodeFlags_None)){
            ImGui::TextColored(Col::SUBTEXT, "One key, key prefix, pasted line, or substring per line.");
            ImGui::InputTextMultiline("##wcIgnoreList", ignoreList, sizeof(ignoreList), {-1.f, 78.f});
        } else {
            ImGui::TextColored(Col::SUBTEXT, "Leave this alone unless you need to exclude specific keys.");
        }
    }

    void drawOutputSection(){
        SectionLabel("Output");
        char outputPathBuf[MAX_PATH * 2] = {};
        const std::string outputPath = computedOutputPath();
        if(!outputPath.empty())
            std::snprintf(outputPathBuf, sizeof(outputPathBuf), "%s", outputPath.c_str());

        if(BeginSectionTable("##wcOutputGrid", 92.f, 86.f)){
            BeginSectionRow("Output");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##wcoutpath", outputPathBuf, sizeof(outputPathBuf), ImGuiInputTextFlags_ReadOnly);
            NextSectionAction();
            if(outputPath.empty()) ImGui::BeginDisabled();
            if(ImGui::Button("Open##wcout"))
                OpenInExplorer(outputPath);
            if(outputPath.empty()) ImGui::EndDisabled();
            EndSectionTable();
        }
        if(outputPath.empty())
            ImGui::TextColored(Col::SUBTEXT, "Select a GER file to resolve the output path.");
        else
            ImGui::TextColored(Col::SUBTEXT, "Generated file is written beside the source file in its Output folder.");
    }

    void drawInlineOutputRow(const char* tableId, const char* inputId, const char* buttonId){
        char outputPathBuf[MAX_PATH * 2] = {};
        const std::string outputPath = computedOutputPath();
        if(!outputPath.empty())
            std::snprintf(outputPathBuf, sizeof(outputPathBuf), "%s", outputPath.c_str());

        if(BeginSectionTable(tableId, 126.f, 86.f)){
            BeginSectionRow("Output");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText(inputId, outputPathBuf, sizeof(outputPathBuf), ImGuiInputTextFlags_ReadOnly);
            NextSectionAction();
            if(outputPath.empty()) ImGui::BeginDisabled();
            if(ImGui::Button(buttonId))
                OpenInExplorer(outputPath);
            if(outputPath.empty()) ImGui::EndDisabled();
            EndSectionTable();
        }
    }

    void drawRunAndLogSections(bool includeLog = true){
        SectionLabel("Run");
        bool busy = running.load();
        bool canRun = canStartAction();
        if(!canRun) ImGui::BeginDisabled();
        if(RunButton("Run##wc")) startAction();
        if(!canRun) ImGui::EndDisabled();
        ImGui::SameLine();
        if(!lastOut.empty() && ImGui::Button("Open Output", {110,32})) OpenInExplorer(lastOut);
        if(busy){ ImGui::SameLine(); ImGui::TextColored(Col::YELLOW, "Running..."); }

        if(!includeLog)
            return;

        SectionLabel("Log");
        std::string logText = log.get();
        ReadOnlyLogBox("##wclog", logText, {-1.f, 0.f});
    }

    GunTypeColourSettings buildCategorySettings(int idx) const {
        const auto& def = gunTypes[idx];
        const auto& st = categories[idx];
        GunTypeColourSettings out;
        out.categoryToken = def.categoryToken;
        out.enabled = st.enabled;
        out.fontTag = fontLabel(st.fontIdx);
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
        out.fontTag = fontLabel(st.fontIdx);
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
    bool canStartAction() const { return !running.load() && inventoryIntPath[0]; }

    void startAction(){
        if(canStartAction()) runProcess();
    }

    void cancelAction(){
        if(!running.load()) return;
        cancelRequested = true;
        log.append("Cancelling...");
    }

    void drawSpecificWeaponsSection(){
        SectionLabel("Specific Weapon Overrides");
        SectionNote("Use this only when one weapon needs to break away from its category rule.");

        ImGui::Text("InventoryItemTypes:"); ImGui::SameLine();
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

        ImGui::Text("Search:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(200.f);
        ImGui::InputText("##wcWeaponSearch", weaponSearch, sizeof(weaponSearch));
        ImGui::SameLine();
        ImGui::Text("Class:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(160.f);
        if(ImGui::BeginCombo("##wcSpecificClass", categoryFilterLabel(specificCategoryFilterIdx))){
            if(ImGui::Selectable("All Classes", specificCategoryFilterIdx == 0))
                specificCategoryFilterIdx = 0;
            if(specificCategoryFilterIdx == 0) ImGui::SetItemDefaultFocus();
            for(int i = 0; i < (int)gunTypes.size(); ++i){
                const bool selected = (specificCategoryFilterIdx == i + 1);
                if(ImGui::Selectable(gunTypes[i].label.c_str(), selected))
                    specificCategoryFilterIdx = i + 1;
                if(selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::Text("Sort:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(170.f);
        if(ImGui::BeginCombo("##wcSpecificSort", SPECIFIC_SORTS[specificSortIdx])){
            for(int i = 0; i < 2; ++i){
                if(ImGui::Selectable(SPECIFIC_SORTS[i], specificSortIdx == i)){
                    specificSortIdx = i;
                    sortSpecificWeapons();
                }
                if(specificSortIdx == i) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if(ImGui::SmallButton("All Visible##wcSpecific")){
            for(auto& st : specificWeapons)
                if(isSpecificVisible(st)) st.enabled = true;
        }
        ImGui::SameLine();
        if(ImGui::SmallButton("None Visible##wcSpecific")){
            for(auto& st : specificWeapons)
                if(isSpecificVisible(st)) st.enabled = false;
        }
        ImGui::SameLine();
        if(ImGui::SmallButton("Clear Search##wcSpecific"))
            weaponSearch[0] = '\0';

        if(specificWeapons.empty()){
            ImGui::TextColored(Col::SUBTEXT, "No specific weapon list loaded.");
            return;
        }

        const float listHeight = filePath[0] ? 520.f : 560.f;
        if(ImGui::BeginTable("##wcSpecificWeapons", 5,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
            {-1.f, listHeight}))
        {
            ImGui::TableSetupColumn("Apply", ImGuiTableColumnFlags_WidthFixed, 52.f);
            ImGui::TableSetupColumn("Weapon", ImGuiTableColumnFlags_WidthFixed, 340.f);
            ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 150.f);
            ImGui::TableSetupColumn("Colours", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Font", ImGuiTableColumnFlags_WidthFixed, 210.f);
            ImGui::TableHeadersRow();

            for(int i = 0; i < (int)specificWeapons.size(); ++i){
                auto& st = specificWeapons[i];
                if(!isSpecificVisible(st))
                    continue;

                ImGui::PushID(i);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Checkbox("##enabled", &st.enabled);

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(st.displayName.c_str());
                ImGui::TextColored(Col::SUBTEXT, "%s", st.categoryToken.c_str());
                if(ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", st.fullKey.c_str());

                ImGui::TableSetColumnIndex(2);
                drawModeCombo("##mode", st);

                ImGui::TableSetColumnIndex(3);
                drawColourControls("specific", i, st);

                ImGui::TableSetColumnIndex(4);
                drawFontCombo("##font", st.fontIdx);

                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }

    void drawSpecificOverridesMergedSection(){
        SectionLabel("Specific Weapon Overrides");
        SectionNote("Use this only when one weapon needs to break away from its category rule.");

        ImGui::Text("InventoryItemTypes:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(-156.f);
        ImGui::InputText("##wcInventoryIntMerged", inventoryIntPath, sizeof(inventoryIntPath));
        ImGui::SameLine();
        if(ImGui::Button("Browse##wcInventoryIntMerged")){
            std::string s;
            if(BrowseFile(s, "INT Files\0*.int;*.INT\0All Files\0*.*\0\0"))
                std::snprintf(inventoryIntPath, sizeof(inventoryIntPath), "%s", s.c_str());
        }
        ImGui::SameLine();
        if(ImGui::Button("Scan##wcInventoryIntMerged"))
            scanSpecificWeapons();

        drawInlineOutputRow("##wcSpecificOutputGrid", "##wcSpecificOutputPath", "Open##wcSpecificOutput");

        ImGui::Text("Search:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(200.f);
        ImGui::InputText("##wcWeaponSearchMerged", weaponSearch, sizeof(weaponSearch));
        ImGui::SameLine();
        ImGui::Text("Class:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(160.f);
        if(ImGui::BeginCombo("##wcSpecificClassMerged", categoryFilterLabel(specificCategoryFilterIdx))){
            if(ImGui::Selectable("All Classes", specificCategoryFilterIdx == 0))
                specificCategoryFilterIdx = 0;
            if(specificCategoryFilterIdx == 0) ImGui::SetItemDefaultFocus();
            for(int i = 0; i < (int)gunTypes.size(); ++i){
                const bool selected = (specificCategoryFilterIdx == i + 1);
                if(ImGui::Selectable(gunTypes[i].label.c_str(), selected))
                    specificCategoryFilterIdx = i + 1;
                if(selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::Text("Sort:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(170.f);
        if(ImGui::BeginCombo("##wcSpecificSortMerged", SPECIFIC_SORTS[specificSortIdx])){
            for(int i = 0; i < 2; ++i){
                if(ImGui::Selectable(SPECIFIC_SORTS[i], specificSortIdx == i)){
                    specificSortIdx = i;
                    sortSpecificWeapons();
                }
                if(specificSortIdx == i) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if(specificWeapons.empty()){
            ImGui::TextColored(Col::SUBTEXT, "No specific weapon list loaded.");
            return;
        }

        const float listHeight = filePath[0] ? 520.f : 560.f;
        if(ImGui::BeginTable("##wcSpecificWeaponsMerged", 5,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
            {-1.f, listHeight}))
        {
            ImGui::TableSetupColumn("Apply", ImGuiTableColumnFlags_WidthFixed, 52.f);
            ImGui::TableSetupColumn("Weapon", ImGuiTableColumnFlags_WidthFixed, 340.f);
            ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 150.f);
            ImGui::TableSetupColumn("Colours", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Font", ImGuiTableColumnFlags_WidthFixed, 210.f);
            ImGui::TableHeadersRow();

            for(int i = 0; i < (int)specificWeapons.size(); ++i){
                auto& st = specificWeapons[i];
                if(!isSpecificVisible(st))
                    continue;

                ImGui::PushID(i);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Checkbox("##enabled", &st.enabled);

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(st.displayName.c_str());
                ImGui::TextColored(Col::SUBTEXT, "%s", st.categoryToken.c_str());
                if(ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", st.fullKey.c_str());

                ImGui::TableSetColumnIndex(2);
                drawModeCombo("##mode", st);

                ImGui::TableSetColumnIndex(3);
                drawColourControls("specific", i, st);

                ImGui::TableSetColumnIndex(4);
                drawFontCombo("##font", st.fontIdx);

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

        if(categoryOrderPending){
            categoryOrderPending = false;
            loadCategoryOrder();
        }

        if(inventoryScanPending && inventoryIntPath[0]){
            inventoryScanPending = false;
            scanSpecificWeapons();
        }

        ImGui::BeginChild("##wc",{0,0},false);
        SectionLabel("Weapon Colour - InventoryItemTypes.GER");
        SectionNote("Build your category colours here, then use Specific Weapon Overrides only when individual weapons need exceptions.");

        drawFileSettingsSection();

        drawPresetConfig();
        drawCategorySection();
        drawIgnoreRulesSection();
        drawRunAndLogSections();
        ImGui::EndChild();
    }

    void drawSpecificOverridesPage(){
        if(autoDetectPending){
            autoDetectPending = false;
            autoDetectDefaultPaths();
        }

        if(categoryOrderPending){
            categoryOrderPending = false;
            loadCategoryOrder();
        }

        if(inventoryScanPending && inventoryIntPath[0]){
            inventoryScanPending = false;
            scanSpecificWeapons();
        }

        ImGui::BeginChild("##wcSpecificPage",{0,0},false);
        SectionLabel("Specific Weapon Overrides - InventoryItemTypes.GER");
        SectionNote("This tool shares the same target file and run output as Weapon Colour. Use it for one-off weapon exceptions.");

        drawSpecificOverridesMergedSection();
        drawRunAndLogSections(false);
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

        std::string path = inventoryIntPath;
        std::string outPath = resolvedOutputPath();
        std::vector<std::string> ignored = ignoredKeys();

        std::thread([this,path,outPath,settings,ignored](){
            try{
                auto r = applyColourToGerFile(path, settings, ignored,
                    [this](const std::string& s){ log.append(s); },
                    &cancelRequested, outPath);
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
