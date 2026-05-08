#pragma once
#include "App.h"
#include "backend/WeaponColour.h"
#include "backend/GradientMaker.h"
#include <array>
#include <vector>
#include <string>
#include <sstream>
#include <cstring>

namespace apb::gui {

struct PageWeaponColour {
    struct CategoryState {
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

    char filePath[MAX_PATH] = {};
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

    std::vector<std::string> fonts;
    std::vector<GunTypeDefinition> gunTypes;
    std::vector<CategoryState> categories;

    static constexpr const char* MODES[] = {"SOLID","Stepped","Smooth","Triple Gradient"};

    PageWeaponColour(){
        fonts = availableFonts();
        gunTypes = weaponGunTypes();
        categories.resize(gunTypes.size());
        if(!categories.empty())
            categories[0].enabled = true; // Marksman, matching the common edit example.
        for(size_t i = 0; i < categories.size(); ++i)
            applyDefaultPalette((int)i);
    }

    static RGB rgbFromFloats(const float c[3]){
        return {c[0], c[1], c[2]};
    }

    static void copyColor(float dst[3], const float src[3]){
        dst[0]=src[0]; dst[1]=src[1]; dst[2]=src[2];
    }

    void applyDefaultPalette(int idx){
        if(idx < 0 || idx >= (int)categories.size()) return;
        auto& st = categories[idx];
        RGB s = rgbFromFloats(st.smoothStart);
        RGB e = rgbFromFloats(st.smoothEnd);
        for(int i = 0; i < 6; ++i){
            RGB c = lerpRGB(s, e, double(i) / 5.0);
            st.stepped[i][0] = (float)c.r;
            st.stepped[i][1] = (float)c.g;
            st.stepped[i][2] = (float)c.b;
        }
    }

    void drawColourControls(int idx){
        auto& st = categories[idx];
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

    void applyPresetToCategory(CategoryState& st){
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

    void drawPresetConfig(){
        SectionLabel("Preset Config");
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::ITEM_BG);
        ImGui::BeginChild("##wcPresetConfig", {0.f, 118.f}, true);

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
        if(ImGui::Button("Apply to Enabled##wcPreset", {142.f, 28.f})){
            for(auto& st : categories)
                if(st.enabled) applyPresetToCategory(st);
        }
        ImGui::SameLine();
        if(ImGui::Button("Apply to All##wcPreset", {118.f, 28.f})){
            for(auto& st : categories) applyPresetToCategory(st);
        }
        ImGui::SameLine();
        ImGui::TextColored(Col::SUBTEXT, "Saved with app config when OK is pressed.");

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    GunTypeColourSettings buildSettings(int idx) const {
        const auto& def = gunTypes[idx];
        const auto& st = categories[idx];
        GunTypeColourSettings out;
        out.categoryToken = def.categoryToken;
        out.enabled = st.enabled;
        out.mode = st.modeIdx == 0 ? ColourMode::SOLID :
                   st.modeIdx == 1 ? ColourMode::STEPPED :
                   st.modeIdx == 2 ? ColourMode::SMOOTH :
                                      ColourMode::TRIPLE;
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

    void draw(){
        ImGui::BeginChild("##wc",{0,0},false);
        SectionLabel("Weapon Colour - InventoryItemTypes.GER");

        ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::ITEM_BG);
        ImGui::BeginChild("##wcFileConfig", {0.f, 74.f}, true);
        ImGui::Text("GER File:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(-80);
        ImGui::InputText("##wcpath",filePath,sizeof(filePath));
        ImGui::SameLine();
        if(ImGui::Button("Browse##wc")){
            std::string s;
            if(BrowseFile(s,"GER Files\0*.ger;*.GER\0All Files\0*.*\0\0"))
                std::snprintf(filePath, sizeof(filePath), "%s", s.c_str());
        }

        ImGui::Text("Font:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(360);
        if(fontIdx < 0 || fontIdx >= (int)fonts.size()) fontIdx = 0;
        if(ImGui::BeginCombo("##wcfont",fonts.empty() ? "None" : fonts[fontIdx].c_str())){
            for(int i=0;i<(int)fonts.size();++i){
                if(ImGui::Selectable(fonts[i].c_str(),fontIdx==i)) fontIdx=i;
                if(fontIdx==i) ImGui::SetItemDefaultFocus();
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

        SectionLabel("Weapon Categories");

        if(ImGui::BeginTable("##wcCategories", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
            {-1.f, 300.f}))
        {
            ImGui::TableSetupColumn("Apply", ImGuiTableColumnFlags_WidthFixed, 52.f);
            ImGui::TableSetupColumn("Gun type", ImGuiTableColumnFlags_WidthFixed, 170.f);
            ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 150.f);
            ImGui::TableSetupColumn("Colours", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for(int i=0;i<(int)gunTypes.size();++i){
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
                ImGui::SetNextItemWidth(-1.f);
                if(ImGui::BeginCombo("##mode", MODES[st.modeIdx])){
                    for(int m=0;m<4;++m){
                        if(ImGui::Selectable(MODES[m], st.modeIdx==m))
                            st.modeIdx = m;
                        if(st.modeIdx==m) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::TableSetColumnIndex(3);
                drawColourControls(i);

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();
        if(ImGui::CollapsingHeader("Ignore List", ImGuiTreeNodeFlags_DefaultOpen)){
            ImGui::TextColored(Col::SUBTEXT, "One key, key prefix, pasted line, or substring per line.");
            ImGui::InputTextMultiline("##wcIgnoreList", ignoreList, sizeof(ignoreList), {-1.f, 78.f});
        }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        bool busy=running.load();
        if(busy) ImGui::BeginDisabled();
        if(RunButton("Run##wc")) startAction();
        if(busy) ImGui::EndDisabled();
        ImGui::SameLine();
        if(!lastOut.empty()&&ImGui::Button("Open Output",{110,32})) OpenInExplorer(lastOut);
        if(busy){ ImGui::SameLine(); ImGui::TextColored(Col::YELLOW,"Running..."); }

        ImGui::Spacing();
        std::string logText=log.get();
        ImGui::InputTextMultiline("##wclog",(char*)logText.c_str(),logText.size()+1,{-1,0},ImGuiInputTextFlags_ReadOnly);
        ImGui::EndChild();
    }

    void runProcess(){
        bool anyEnabled = false;
        std::vector<GunTypeColourSettings> settings;
        settings.reserve(categories.size());
        for(int i=0;i<(int)categories.size();++i){
            settings.push_back(buildSettings(i));
            anyEnabled = anyEnabled || categories[i].enabled;
        }

        if(!anyEnabled){
            log.append("No gun types selected.");
            return;
        }

        log.clear();
        cancelRequested = false;
        running=true;
        lastOut.clear();

        std::string path=filePath;
        std::string font=(fontIdx>=0 && fontIdx<(int)fonts.size()) ? fonts[fontIdx] : "None";
        std::vector<std::string> ignored=ignoredKeys();

        std::thread([this,path,settings,font,ignored](){
            try{
                auto r=applyColourToGerFile(path,settings,font,ignored,
                    [this](const std::string& s){log.append(s);},
                    &cancelRequested);
                if(r.cancelled || cancelRequested.load()){
                    log.append("Cancelled.");
                } else {
                    lastOut=r.outputPath;
                    char buf[320];
                    std::snprintf(buf,sizeof(buf),"Done: %d recoloured, %d skipped -> %s",
                        r.newlyColoured,r.skippedRules,r.outputPath.c_str());
                    log.append(buf);
                }
            }catch(std::exception& e){
                log.append(std::string("Error: ")+e.what());
            }
            cancelRequested = false;
            running=false;
        }).detach();
    }
};

} // namespace apb::gui
