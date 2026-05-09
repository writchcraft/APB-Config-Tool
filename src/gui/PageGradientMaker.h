#pragma once
#include "App.h"
#include "backend/GradientMaker.h"
#include "backend/ThemeLibrary.h"
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cctype>

namespace apb::gui {

struct PageGradientMaker {
    enum class ThemeSaveSource { Stepped, Smooth, Triple };
    enum class ActiveTab { Stepped, Smooth, Triple };

    // Stepped tab state
    int   themeIdxHard   = 0;   // index into ThemeLib().themes, or themes.size() = Custom
    float hardCols[8][3] = {};  // up to 8 colours (theme may have 2–8)
    int   hardColCount   = 6;
    char  hardInput[2048]  = {};
    char  hardOutput[4096] = {};

    // Smooth tab state
    int   themeIdxSmooth  = 0;
    float smoothStart[3]  = {};
    float smoothEnd[3]    = {};
    char  smoothInput[2048]  = {};
    char  smoothOutput[4096] = {};

    // Triple-gradient tab state
    int   themeIdxTriple = 0;
    float tripleStart[3] = {};
    float tripleMid[3]   = {};
    float tripleEnd[3]   = {};
    char  tripleInput[2048]  = {};
    char  tripleOutput[4096] = {};

    char  themeName[128] = {};
    std::string themeStatus;
    bool  themeStatusOk = true;
    bool  needsReload = true;   // reload themes on first draw
    bool  openThemeNamePopup = false;
    ActiveTab activeTab = ActiveTab::Stepped;

    PageGradientMaker(){ }

    static std::string trim(const std::string& value){
        size_t start = 0;
        while(start < value.size() && std::isspace((unsigned char)value[start])) ++start;
        size_t end = value.size();
        while(end > start && std::isspace((unsigned char)value[end - 1])) --end;
        return value.substr(start, end - start);
    }

    static std::string escapeJson(const std::string& value){
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

    static bool isReservedThemeName(const std::string& value){
        if(value.size() != 6) return false;
        return std::tolower((unsigned char)value[0]) == 'c' &&
               std::tolower((unsigned char)value[1]) == 'u' &&
               std::tolower((unsigned char)value[2]) == 's' &&
               std::tolower((unsigned char)value[3]) == 't' &&
               std::tolower((unsigned char)value[4]) == 'o' &&
               std::tolower((unsigned char)value[5]) == 'm';
    }

    static void writeColor(std::ostream& out, const RGB& color){
        out << "[" << color.r << ", " << color.g << ", " << color.b << "]";
    }

    static RGB rgbFromFloats(const float color[3]){
        return {color[0], color[1], color[2]};
    }

    static std::string sanitizeFileStem(const std::string& name){
        std::string stem;
        stem.reserve(name.size());
        for(char ch : name){
            if(std::isalnum((unsigned char)ch)) stem += (char)std::tolower((unsigned char)ch);
            else if(ch == ' ' || ch == '-' || ch == '_') stem += '_';
        }
        while(!stem.empty() && stem.back() == '_') stem.pop_back();
        size_t lead = 0;
        while(lead < stem.size() && stem[lead] == '_') ++lead;
        stem.erase(0, lead);
        return stem.empty() ? "theme" : stem;
    }

    static RGB sampleSteppedPalette(const std::vector<RGB>& colors, double t){
        if(colors.empty()) return {1.0, 1.0, 1.0};
        if(colors.size() == 1) return colors.front();
        if(t <= 0.0) return colors.front();
        if(t >= 1.0) return colors.back();

        double pos = t * double(colors.size() - 1);
        int idx = (int)pos;
        if(idx >= (int)colors.size() - 1) return colors.back();
        double local = pos - idx;
        return lerpRGB(colors[idx], colors[idx + 1], local);
    }

    std::vector<RGB> buildSteppedFromSmooth() const {
        std::vector<RGB> out;
        RGB start = rgbFromFloats(smoothStart);
        RGB end = rgbFromFloats(smoothEnd);
        for(int i = 0; i < 6; ++i)
            out.push_back(lerpRGB(start, end, double(i) / 5.0));
        return out;
    }

    std::vector<RGB> buildSteppedFromTriple() const {
        std::vector<RGB> out;
        RGB start = rgbFromFloats(tripleStart);
        RGB mid = rgbFromFloats(tripleMid);
        RGB end = rgbFromFloats(tripleEnd);
        for(int i = 0; i < 6; ++i){
            double t = double(i) / 5.0;
            out.push_back(t <= 0.5 ? lerpRGB(start, mid, t * 2.0)
                                   : lerpRGB(mid, end, (t - 0.5) * 2.0));
        }
        return out;
    }

    std::vector<RGB> currentSteppedPalette() const {
        std::vector<RGB> out;
        for(int i = 0; i < hardColCount; ++i)
            out.push_back(rgbFromFloats(hardCols[i]));
        return out;
    }

    void setThemeName(const std::string& name){
        std::snprintf(themeName, sizeof(themeName), "%s", name.c_str());
    }

    void setThemeStatus(const std::string& message, bool ok){
        themeStatus = message;
        themeStatusOk = ok;
    }

    void requestThemeNamePopup(){
        themeStatus.clear();
        themeStatusOk = false;
        openThemeNamePopup = true;
    }

    void drawThemeNamePopup(){
        if(openThemeNamePopup){
            ImGui::OpenPopup("##themeNameRequired");
            openThemeNamePopup = false;
        }

        ImGui::SetNextWindowSize({320.f, 0.f}, ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
            ImGuiCond_Appearing, {0.5f, 0.5f});
        if(ImGui::BeginPopupModal("##themeNameRequired", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse))
        {
            ImGui::TextUnformatted("Please name this theme");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::SetCursorPosX((ImGui::GetWindowSize().x - 96.f) * 0.5f);
            if(RunButton("OK##themeNameRequired", 96.f))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // ── Apply a theme to the stepped colours ──────────────────────────────
    void applyThemeHard(const GradientTheme& t){
        hardColCount = (int)t.stepped.size();
        if(hardColCount < 2) hardColCount = 2;
        if(hardColCount > 8) hardColCount = 8;
        for(int i = 0; i < hardColCount; ++i){
            hardCols[i][0] = (float)t.stepped[i].r;
            hardCols[i][1] = (float)t.stepped[i].g;
            hardCols[i][2] = (float)t.stepped[i].b;
        }
    }

    void applyThemeSmooth(const GradientTheme& t){
        smoothStart[0]=(float)t.smoothStart.r;
        smoothStart[1]=(float)t.smoothStart.g;
        smoothStart[2]=(float)t.smoothStart.b;
        smoothEnd[0]=(float)t.smoothEnd.r;
        smoothEnd[1]=(float)t.smoothEnd.g;
        smoothEnd[2]=(float)t.smoothEnd.b;
    }

    void applyThemeTriple(const GradientTheme& t){
        RGB start = t.smoothStart;
        RGB end = t.smoothEnd;
        RGB mid = t.tripleMiddle;

        tripleStart[0]=(float)start.r;
        tripleStart[1]=(float)start.g;
        tripleStart[2]=(float)start.b;
        tripleMid[0]=(float)mid.r;
        tripleMid[1]=(float)mid.g;
        tripleMid[2]=(float)mid.b;
        tripleEnd[0]=(float)end.r;
        tripleEnd[1]=(float)end.g;
        tripleEnd[2]=(float)end.b;
    }

    void syncThemeSelectionsByName(const std::string& name){
        auto& lib = ThemeLib();
        int idx = lib.count();
        for(int i = 0; i < lib.count(); ++i){
            if(lib.themes[i].name == name){
                idx = i;
                break;
            }
        }
        themeIdxHard = idx;
        themeIdxSmooth = idx;
        themeIdxTriple = idx;
        if(idx < lib.count()){
            applyThemeHard(lib.themes[idx]);
            applyThemeSmooth(lib.themes[idx]);
            applyThemeTriple(lib.themes[idx]);
        }
    }

    bool saveTheme(ThemeSaveSource source){
        const std::string name = trim(themeName);
        if(name.empty() || isReservedThemeName(name)){
            requestThemeNamePopup();
            return false;
        }

        std::vector<RGB> stepped;
        RGB smoothA{}, smoothB{}, tripleM{};
        switch(source){
            case ThemeSaveSource::Stepped: {
                stepped = currentSteppedPalette();
                smoothA = stepped.front();
                smoothB = stepped.back();
                tripleM = sampleSteppedPalette(stepped, 0.5);
                break;
            }
            case ThemeSaveSource::Smooth: {
                smoothA = rgbFromFloats(smoothStart);
                smoothB = rgbFromFloats(smoothEnd);
                stepped = buildSteppedFromSmooth();
                tripleM = lerpRGB(smoothA, smoothB, 0.5);
                break;
            }
            case ThemeSaveSource::Triple: {
                smoothA = rgbFromFloats(tripleStart);
                smoothB = rgbFromFloats(tripleEnd);
                tripleM = rgbFromFloats(tripleMid);
                stepped = buildSteppedFromTriple();
                break;
            }
        }

        try{
            std::filesystem::create_directories(ThemeLibrary::themesDir());
            const std::filesystem::path path =
                std::filesystem::path(ThemeLibrary::themesDir()) / (sanitizeFileStem(name) + ".json");

            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if(!out){
                setThemeStatus("Could not write theme file.", false);
                return false;
            }

            out << "{\n";
            out << "    \"name\": \"" << escapeJson(name) << "\",\n";
            out << "    \"stepped\": [\n";
            for(size_t i = 0; i < stepped.size(); ++i){
                out << "        ";
                writeColor(out, stepped[i]);
                out << (i + 1 < stepped.size() ? ",\n" : "\n");
            }
            out << "    ],\n";
            out << "    \"smooth_start\": ";
            writeColor(out, smoothA);
            out << ",\n";
            out << "    \"smooth_end\": ";
            writeColor(out, smoothB);
            out << ",\n";
            out << "    \"triple_middle\": ";
            writeColor(out, tripleM);
            out << "\n";
            out << "}\n";
            out.close();

            ThemeLib().reload();
            syncThemeSelectionsByName(name);
            setThemeStatus("Theme saved.", true);
            return true;
        }catch(...){
            setThemeStatus("Theme save failed.", false);
            return false;
        }
    }

    void drawThemeControls(const char* inputId, const char* comboId, const char* buttonId,
                           int& themeIdx, int nThemes, const std::vector<std::string>& names,
                           const std::function<void(int)>& applyTheme, ThemeSaveSource source){
        constexpr float totalWidth = 360.f;
        constexpr const char* CUSTOM_DISPLAY = "Custom (unsaved)";

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Theme:");
        ImGui::SameLine();

        const float arrowWidth = ImGui::GetFrameHeight();
        ImGui::PushID(comboId);
        ImGui::BeginGroup();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.f, 0.f});

        ImGui::SetNextItemWidth(totalWidth - arrowWidth);
        ImGui::InputText(inputId, themeName, sizeof(themeName));
        ImGui::SameLine();
        if(ImGui::ArrowButton("##themeDropdown", ImGuiDir_Down)){
            ImGui::OpenPopup("##themePopup");
        }

        ImGui::PopStyleVar();

        ImGui::SetNextWindowSize({totalWidth, 0.f}, ImGuiCond_Appearing);
        if(ImGui::BeginPopup("##themePopup")){
            for(int i = 0; i < (int)names.size(); ++i){
                const bool selected = (themeIdx == i);
                const char* label = (i == nThemes) ? CUSTOM_DISPLAY : names[i].c_str();
                ImGui::PushID(i);
                if(ImGui::Selectable(label, selected)){
                    if(i < nThemes){
                        themeIdx = i;
                        setThemeName(names[i]);
                        applyTheme(i);
                    } else {
                        themeIdx = nThemes;
                        themeName[0] = '\0';
                    }
                }
                ImGui::PopID();
                if(selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndPopup();
        }

        ImGui::EndGroup();
        ImGui::PopID();
        ImGui::SameLine();
        if(ImGui::Button(buttonId, {96.f, 28.f}))
            saveTheme(source);

        if(!themeStatus.empty()){
            ImGui::PushStyleColor(ImGuiCol_Text, themeStatusOk ? Col::GREEN : Col::RED);
            ImGui::TextUnformatted(themeStatus.c_str());
            ImGui::PopStyleColor();
        }
    }

    bool isActionRunning() const { return false; }
    bool canStartAction() const { return true; }
    void cancelAction() const {}

    void generateStepped(){
        std::vector<RGB> pal;
        pal.reserve(hardColCount);
        for(int i=0;i<hardColCount;++i)
            pal.push_back({hardCols[i][0],hardCols[i][1],hardCols[i][2]});
        auto res = hardGradientString(hardInput, pal);
        snprintf(hardOutput,sizeof(hardOutput),"%s",res.c_str());
    }

    void generateSmooth(){
        RGB s={smoothStart[0],smoothStart[1],smoothStart[2]};
        RGB e={smoothEnd[0],smoothEnd[1],smoothEnd[2]};
        auto res = smoothGradientString(smoothInput, s, e);
        snprintf(smoothOutput,sizeof(smoothOutput),"%s",res.c_str());
    }

    void generateTriple(){
        RGB a={tripleStart[0],tripleStart[1],tripleStart[2]};
        RGB b={tripleMid[0],tripleMid[1],tripleMid[2]};
        RGB c={tripleEnd[0],tripleEnd[1],tripleEnd[2]};
        auto res = tripleGradientString(tripleInput, a, b, c);
        snprintf(tripleOutput,sizeof(tripleOutput),"%s",res.c_str());
    }

    void startAction(){
        switch(activeTab){
            case ActiveTab::Stepped: generateStepped(); break;
            case ActiveTab::Smooth:  generateSmooth(); break;
            case ActiveTab::Triple:  generateTriple(); break;
        }
    }

    // ── Draw ──────────────────────────────────────────────────────────────
    void draw(){
        // Reload themes if needed (first frame or after Refresh)
        if(needsReload){
            ThemeLib().reload();
            needsReload = false;
            // Apply first theme by default if available
            auto& lib = ThemeLib();
            if(!lib.themes.empty()){
                applyThemeHard(lib.themes[0]);
                applyThemeSmooth(lib.themes[0]);
                applyThemeTriple(lib.themes[0]);
                themeIdxHard   = 0;
                themeIdxSmooth = 0;
                themeIdxTriple = 0;
            }
        }

        auto& lib = ThemeLib();
        int nThemes = lib.count();
        int CUSTOM  = nThemes;  // sentinel index for "Custom"

        // Build combo label list (static per-frame rebuild is cheap at this size)
        std::vector<std::string> names = lib.nameList(true); // includes "Custom"

        ImGui::BeginChild("##gm",{0,0},false);
        SectionLabel("Gradient Maker");
        SectionNote("Build stepped, smooth, or triple gradients and keep reusable theme presets grouped with each generator mode.");

        // ── Refresh button ────────────────────────────────────────────────
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80.f);
        if(ImGui::SmallButton("Reload Themes")){
            needsReload = true;
        }
        if(!lib.error.empty()){
            ImGui::PushStyleColor(ImGuiCol_Text, Col::RED);
            ImGui::TextWrapped("Theme error: %s", lib.error.c_str());
            ImGui::PopStyleColor();
        }

        if(ImGui::BeginTabBar("##gmtabs")){

            // ══════════════════════════════════════════════════════════════
            // Stepped (N-colour) tab
            // ══════════════════════════════════════════════════════════════
            if(ImGui::BeginTabItem("Stepped")){
                activeTab = ActiveTab::Stepped;

                SectionLabel("Theme");
                drawThemeControls("##themeNameH", "##themeH", "Save Theme##h",
                    themeIdxHard, nThemes, names,
                    [&](int idx){ applyThemeHard(lib.themes[idx]); },
                    ThemeSaveSource::Stepped);
                ImGui::SameLine();
                // Open Themes folder button
                if(ImGui::SmallButton("Open Folder##h")){
                    std::string dir = ThemeLibrary::themesDir();
                    ShellExecuteA(nullptr,"explore",dir.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
                }

                SectionLabel("Colours");
                ImGui::Text("Palette (%d):", hardColCount);
                for(int i = 0; i < hardColCount; ++i){
                    char id[16]; snprintf(id,sizeof(id),"##hc%d",i);
                    if(ColorPickerButton(id, hardCols[i])){
                        themeIdxHard = CUSTOM; // user edited → mark Custom
                    }
                    if(i < hardColCount-1) ImGui::SameLine();
                }

                SectionLabel("Input");
                ImGui::InputTextMultiline("##hinput",hardInput,sizeof(hardInput),{-1,80});
                if(RunButton("Generate##h")) generateStepped();
                ImGui::SameLine();
                if(ImGui::Button("Copy##h",{80,28})) ImGui::SetClipboardText(hardOutput);

                SectionLabel("Output");
                ReadOnlyLogBox("##hout", hardOutput, {-1.f, 120.f});
                ImGui::EndTabItem();
            }

            // ══════════════════════════════════════════════════════════════
            // Smooth (2-colour) tab
            // ══════════════════════════════════════════════════════════════
            if(ImGui::BeginTabItem("Smooth (2-colour)")){
                activeTab = ActiveTab::Smooth;

                SectionLabel("Theme");
                drawThemeControls("##themeNameS", "##themeS", "Save Theme##s",
                    themeIdxSmooth, nThemes, names,
                    [&](int idx){ applyThemeSmooth(lib.themes[idx]); },
                    ThemeSaveSource::Smooth);
                ImGui::SameLine();
                if(ImGui::SmallButton("Open Folder##s")){
                    std::string dir = ThemeLibrary::themesDir();
                    ShellExecuteA(nullptr,"explore",dir.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
                }

                SectionLabel("Colours");
                ImGui::Text("Start:"); ImGui::SameLine();
                if(ColorPickerButton("##ss", smoothStart)) themeIdxSmooth = CUSTOM;
                ImGui::SameLine();
                ImGui::Text("End:"); ImGui::SameLine();
                if(ColorPickerButton("##se", smoothEnd))   themeIdxSmooth = CUSTOM;

                SectionLabel("Input");
                ImGui::InputTextMultiline("##sinput",smoothInput,sizeof(smoothInput),{-1,80});
                if(RunButton("Generate##s")) generateSmooth();
                ImGui::SameLine();
                if(ImGui::Button("Copy##s",{80,28})) ImGui::SetClipboardText(smoothOutput);

                SectionLabel("Output");
                ReadOnlyLogBox("##sout", smoothOutput, {-1.f, 120.f});
                ImGui::EndTabItem();
            }

            if(ImGui::BeginTabItem("Triple Gradient")){
                activeTab = ActiveTab::Triple;

                SectionLabel("Theme");
                drawThemeControls("##themeNameT", "##themeT", "Save Theme##t",
                    themeIdxTriple, nThemes, names,
                    [&](int idx){ applyThemeTriple(lib.themes[idx]); },
                    ThemeSaveSource::Triple);
                ImGui::SameLine();
                if(ImGui::SmallButton("Open Folder##t")){
                    std::string dir = ThemeLibrary::themesDir();
                    ShellExecuteA(nullptr,"explore",dir.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
                }

                SectionLabel("Colours");
                ImGui::Text("Colour 1:"); ImGui::SameLine();
                if(ColorPickerButton("##ts", tripleStart)) themeIdxTriple = CUSTOM;
                ImGui::SameLine();
                ImGui::Text("Colour 2:"); ImGui::SameLine();
                if(ColorPickerButton("##tm", tripleMid)) themeIdxTriple = CUSTOM;
                ImGui::SameLine();
                ImGui::Text("Colour 3:"); ImGui::SameLine();
                if(ColorPickerButton("##te", tripleEnd)) themeIdxTriple = CUSTOM;

                SectionLabel("Input");
                ImGui::InputTextMultiline("##tinput",tripleInput,sizeof(tripleInput),{-1,80});
                if(RunButton("Generate##t")) generateTriple();
                ImGui::SameLine();
                if(ImGui::Button("Copy##t",{80,28})) ImGui::SetClipboardText(tripleOutput);

                SectionLabel("Output");
                ReadOnlyLogBox("##tout", tripleOutput, {-1.f, 120.f});
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
        drawThemeNamePopup();
        ImGui::EndChild();
    }
};

} // namespace apb::gui
