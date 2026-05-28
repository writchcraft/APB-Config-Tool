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
        (void)inputId;
        (void)buttonId;
        (void)source;

        if(themeIdx < 0 || themeIdx > nThemes)
            themeIdx = nThemes;

        const char* preview = (themeIdx >= 0 && themeIdx < (int)names.size())
            ? names[themeIdx].c_str() : "Custom";

        if(ImGui::BeginCombo(comboId, preview)){
            for(int i = 0; i < (int)names.size(); ++i){
                const bool selected = (themeIdx == i);
                if(ImGui::Selectable(names[i].c_str(), selected)){
                    themeIdx = i;
                    if(i < nThemes)
                        applyTheme(i);
                }
                if(selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    static ImU32 packColor(const float color[3]){
        const int r = std::max(0, std::min(255, (int)(color[0] * 255.f + 0.5f)));
        const int g = std::max(0, std::min(255, (int)(color[1] * 255.f + 0.5f)));
        const int b = std::max(0, std::min(255, (int)(color[2] * 255.f + 0.5f)));
        return IM_COL32(r, g, b, 255);
    }

    static ImU32 packColor(const RGB& color){
        const int r = std::max(0, std::min(255, (int)(color.r * 255.0 + 0.5)));
        const int g = std::max(0, std::min(255, (int)(color.g * 255.0 + 0.5)));
        const int b = std::max(0, std::min(255, (int)(color.b * 255.0 + 0.5)));
        return IM_COL32(r, g, b, 255);
    }

    static std::string colorHex(const float color[3]){
        char buf[16];
        const int r = std::max(0, std::min(255, (int)(color[0] * 255.f + 0.5f)));
        const int g = std::max(0, std::min(255, (int)(color[1] * 255.f + 0.5f)));
        const int b = std::max(0, std::min(255, (int)(color[2] * 255.f + 0.5f)));
        std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
        return buf;
    }

    static std::string colorRgbText(const float color[3]){
        char buf[48];
        const int r = std::max(0, std::min(255, (int)(color[0] * 255.f + 0.5f)));
        const int g = std::max(0, std::min(255, (int)(color[1] * 255.f + 0.5f)));
        const int b = std::max(0, std::min(255, (int)(color[2] * 255.f + 0.5f)));
        std::snprintf(buf, sizeof(buf), "RGB(%d, %d, %d)", r, g, b);
        return buf;
    }

    const char* activeModeLabel() const{
        switch(activeTab){
            case ActiveTab::Stepped: return "Stepped";
            case ActiveTab::Smooth: return "Smooth 2-Colour";
            case ActiveTab::Triple: return "Triple Gradient";
        }
        return "Stepped";
    }

    int& activeThemeIndex(){
        switch(activeTab){
            case ActiveTab::Stepped: return themeIdxHard;
            case ActiveTab::Smooth:  return themeIdxSmooth;
            case ActiveTab::Triple:  return themeIdxTriple;
        }
        return themeIdxHard;
    }

    void applyThemeForActiveMode(const GradientTheme& theme){
        switch(activeTab){
            case ActiveTab::Stepped: applyThemeHard(theme); break;
            case ActiveTab::Smooth:  applyThemeSmooth(theme); break;
            case ActiveTab::Triple:  applyThemeTriple(theme); break;
        }
    }

    bool drawModeButton(const char* label, ActiveTab mode){
        const bool selected = (activeTab == mode);
        if(selected){
            ImGui::PushStyleColor(ImGuiCol_Button,        Col::BTN_OK);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Col::YELLOW_DIM);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Col::YELLOW_DIM);
            ImGui::PushStyleColor(ImGuiCol_Text,          {0.04f,0.04f,0.04f,1.f});
            ImGui::PushStyleColor(ImGuiCol_Border,        Col::YELLOW_DIM);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        Col::BTN_DARK);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Col::BTN_HOV);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.36f,0.36f,0.36f,1.f});
            ImGui::PushStyleColor(ImGuiCol_Text,          Col::TEXT);
            ImGui::PushStyleColor(ImGuiCol_Border,        Col::BORDER_DIM);
        }
        const bool pressed = ImGui::Button(label, {-FLT_MIN, 32.f});
        ImGui::PopStyleColor(5);
        if(pressed)
            activeTab = mode;
        return pressed;
    }

    void drawHeader(ThemeLibrary& lib){
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::BG);
        ImGui::BeginChild("##gmHeader", {0.f, 104.f}, true);
        if(ImGui::BeginTable("##gmHeaderTable", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
        {
            ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 152.f);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(Col::YELLOW, "Gradient Maker");
            ImGui::TextColored(Col::SUBTEXT, "Build stepped, smooth, or triple APB colour gradients.");

            ImGui::TableSetColumnIndex(1);
            const float btnW = 140.f;
            const float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(std::max(0.f, avail - btnW));
            if(ImGui::Button("Reload Themes", {btnW, 28.f}))
                needsReload = true;

            if(!lib.error.empty()){
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, Col::RED);
                ImGui::TextWrapped("%s", lib.error.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::EndTable();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void drawModeSelector(){
        ImGui::TextColored(Col::YELLOW, "Mode");
        ImGui::Spacing();
        const bool inlineRow = ImGui::GetContentRegionAvail().x >= 540.f;
        const int columns = inlineRow ? 3 : 1;
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {0.f, 0.f});
        if(ImGui::BeginTable("##gmMode", columns,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
        {
            for(int i = 0; i < columns; ++i)
                ImGui::TableSetupColumn("##col", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            if(inlineRow){
                ImGui::TableSetColumnIndex(0); drawModeButton("Stepped", ActiveTab::Stepped);
                ImGui::TableSetColumnIndex(1); drawModeButton("Smooth 2-Colour", ActiveTab::Smooth);
                ImGui::TableSetColumnIndex(2); drawModeButton("Triple Gradient", ActiveTab::Triple);
            } else {
                ImGui::TableSetColumnIndex(0); drawModeButton("Stepped", ActiveTab::Stepped);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); drawModeButton("Smooth 2-Colour", ActiveTab::Smooth);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); drawModeButton("Triple Gradient", ActiveTab::Triple);
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }

    void drawThemePresetPanel(ThemeLibrary& lib, const std::vector<std::string>& names){
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::BG);
        ImGui::BeginChild("##gmTheme", {0.f, 104.f}, true);
        ImGui::TextColored(Col::YELLOW, "Theme Preset");
        ImGui::TextColored(Col::SUBTEXT, "Applies to the active mode: %s.", activeModeLabel());
        ImGui::Spacing();

        int& themeIdx = activeThemeIndex();
        if(themeIdx < 0 || themeIdx > lib.count())
            themeIdx = lib.count();

        const float avail = ImGui::GetContentRegionAvail().x;
        const bool inlineRow = avail >= 380.f;
        const float buttonW = 118.f;
        if(inlineRow){
            ImGui::SetNextItemWidth(std::max(160.f, avail - buttonW - ImGui::GetStyle().ItemSpacing.x));
            drawThemeControls("##themeName", "##themeCombo", "##themeButton",
                themeIdx, lib.count(), names,
                [&](int idx){ applyThemeForActiveMode(lib.themes[idx]); },
                ThemeSaveSource::Stepped);
            ImGui::SameLine();
            if(ImGui::Button("Open Folder", {buttonW, 28.f})){
                std::string dir = ThemeLibrary::themesDir();
                ShellExecuteA(nullptr, "explore", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        } else {
            ImGui::SetNextItemWidth(-FLT_MIN);
            drawThemeControls("##themeName", "##themeCombo", "##themeButton",
                themeIdx, lib.count(), names,
                [&](int idx){ applyThemeForActiveMode(lib.themes[idx]); },
                ThemeSaveSource::Stepped);
            ImGui::Spacing();
            if(ImGui::Button("Open Folder", {buttonW, 28.f})){
                std::string dir = ThemeLibrary::themesDir();
                ShellExecuteA(nullptr, "explore", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void drawPreviewPanel(ThemeLibrary& lib){
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::BG);
        ImGui::BeginChild("##gmPreview", {0.f, 190.f}, true);
        ImGui::TextColored(Col::YELLOW, "Gradient Preview");
        ImGui::TextColored(Col::SUBTEXT, "Live preview for the active mode.");
        ImGui::Spacing();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 barMin = ImGui::GetCursorScreenPos();
        const ImVec2 barSize = {ImGui::GetContentRegionAvail().x, 42.f};
        ImGui::InvisibleButton("##gmPreviewBar", barSize);
        ImGui::Dummy({0.f, 8.f});
        const ImVec2 barMax = {barMin.x + barSize.x, barMin.y + barSize.y};
        const int customIdx = lib.count();
        bool anyChanged = false;

        const int count = (activeTab == ActiveTab::Stepped) ? std::max(1, hardColCount)
                        : (activeTab == ActiveTab::Smooth)  ? 2 : 3;
        auto drawSwatchTile = [&](int index, const char* label, float color[3], const char* popupId){
            ImGui::PushID(index);
            ImGui::BeginGroup();
            const float swatchW = (activeTab == ActiveTab::Stepped) ? 56.f : 74.f;
            const float swatchH = 20.f;
            bool changed = ColorPickerButton(popupId, color, swatchW, swatchH);
            if(ImGui::IsItemHovered()){
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                dl->AddRect(min, max, IM_COL32(247,209,10,220), 4.f, 0, 1.5f);
            }
            const float textW = ImGui::CalcTextSize(label).x;
            const float labelX = ImGui::GetCursorPosX() + std::max(0.f, (swatchW - textW) * 0.5f);
            ImGui::SetCursorPosX(labelX);
            ImGui::TextColored(Col::SUBTEXT, "%s", label);
            ImGui::EndGroup();
            ImGui::PopID();
            if(changed)
                anyChanged = true;
        };

        const float tileW = (activeTab == ActiveTab::Stepped) ? 56.f : 74.f;
        const float rowSpacing = ImGui::GetStyle().ItemSpacing.x;
        const float avail = ImGui::GetContentRegionAvail().x;
        const int columns = std::max(1, std::min(count, (int)((avail + rowSpacing) / (tileW + rowSpacing))));

        for(int i = 0; i < count; ){
            const int rowCount = std::min(columns, count - i);
            const float rowW = rowCount * tileW + std::max(0, rowCount - 1) * rowSpacing;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, (avail - rowW) * 0.5f));

            for(int row = 0; row < rowCount; ++row, ++i){
                if(row > 0)
                    ImGui::SameLine(0.f, rowSpacing);

                switch(activeTab){
                    case ActiveTab::Stepped: {
                        char label[8];
                        std::snprintf(label, sizeof(label), "%d", i + 1);
                        char popup[24];
                        std::snprintf(popup, sizeof(popup), "##gmPreviewStep%d", i);
                        drawSwatchTile(i, label, hardCols[i], popup);
                        break;
                    }
                    case ActiveTab::Smooth:
                        if(i == 0)
                            drawSwatchTile(0, "Start", smoothStart, "##gmPreviewStart");
                        else
                            drawSwatchTile(1, "End", smoothEnd, "##gmPreviewEnd");
                        break;
                    case ActiveTab::Triple:
                        if(i == 0)
                            drawSwatchTile(0, "1", tripleStart, "##gmPreviewTriple1");
                        else if(i == 1)
                            drawSwatchTile(1, "2", tripleMid, "##gmPreviewTriple2");
                        else
                            drawSwatchTile(2, "3", tripleEnd, "##gmPreviewTriple3");
                        break;
                }
            }

            if(i < count)
                ImGui::NewLine();
        }

        if(anyChanged){
            switch(activeTab){
                case ActiveTab::Stepped: themeIdxHard = customIdx; generateStepped(); break;
                case ActiveTab::Smooth:  themeIdxSmooth = customIdx; generateSmooth(); break;
                case ActiveTab::Triple:  themeIdxTriple = customIdx; generateTriple(); break;
            }
        }

        auto drawSmoothBar = [&](const RGB& a, const RGB& b, const RGB* c, bool triple){
            const int slices = 128;
            for(int i = 0; i < slices; ++i){
                const float t0 = (float)i / (float)slices;
                const float t1 = (float)(i + 1) / (float)slices;
                const float t = (t0 + t1) * 0.5f;
                RGB col = triple
                    ? ((t <= 0.5f) ? lerpRGB(a, *c, t * 2.0)
                                   : lerpRGB(*c, b, (t - 0.5f) * 2.0))
                    : lerpRGB(a, b, t);
                const float x0 = barMin.x + barSize.x * t0;
                const float x1 = barMin.x + barSize.x * t1;
                dl->AddRectFilled({x0, barMin.y}, {x1, barMax.y}, packColor(col));
            }
            dl->AddRect(barMin, barMax, IM_COL32(110,110,110,255));
        };

        if(activeTab == ActiveTab::Stepped){
            const int steppedCount = std::max(1, hardColCount);
            for(int i = 0; i < steppedCount; ++i){
                const float t0 = (float)i / (float)steppedCount;
                const float t1 = (float)(i + 1) / (float)steppedCount;
                const ImVec2 a = {barMin.x + barSize.x * t0, barMin.y};
                const ImVec2 b = {barMin.x + barSize.x * t1, barMax.y};
                dl->AddRectFilled(a, b, packColor(hardCols[i]));
                if(i > 0)
                    dl->AddLine({a.x, a.y}, {a.x, a.y + barSize.y}, IM_COL32(34,34,34,255));
            }
            dl->AddRect(barMin, barMax, IM_COL32(110,110,110,255));
        } else if(activeTab == ActiveTab::Smooth){
            RGB start{smoothStart[0], smoothStart[1], smoothStart[2]};
            RGB end{smoothEnd[0], smoothEnd[1], smoothEnd[2]};
            drawSmoothBar(start, end, nullptr, false);
        } else {
            RGB start{tripleStart[0], tripleStart[1], tripleStart[2]};
            RGB mid{tripleMid[0], tripleMid[1], tripleMid[2]};
            RGB end{tripleEnd[0], tripleEnd[1], tripleEnd[2]};
            drawSmoothBar(start, end, &mid, true);
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    char* activeOutputBuffer(){
        switch(activeTab){
            case ActiveTab::Stepped: return hardOutput;
            case ActiveTab::Smooth:  return smoothOutput;
            case ActiveTab::Triple:  return tripleOutput;
        }
        return hardOutput;
    }

    const char* activeOutputBuffer() const{
        switch(activeTab){
            case ActiveTab::Stepped: return hardOutput;
            case ActiveTab::Smooth:  return smoothOutput;
            case ActiveTab::Triple:  return tripleOutput;
        }
        return hardOutput;
    }

    void drawInputOutputSection(){
        ImGui::TextColored(Col::YELLOW, "Input / Output");
        ImGui::TextColored(Col::SUBTEXT, "The generator output stays in place for copy/paste workflows.");
        ImGui::Spacing();

        const bool sideBySide = ImGui::GetContentRegionAvail().x >= 820.f;
        const float panelH = 220.f;
        auto drawInputCard = [&](const char* id, const char* title, char* buffer, size_t bufferSize){
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::BG);
            ImGui::BeginChild(id, {0.f, panelH}, true);
            ImGui::TextUnformatted(title);
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, Col::ITEM_BG);
            ImGui::InputTextMultiline("##gmInputText", buffer, bufferSize,
                {-FLT_MIN, panelH - 52.f});
            ImGui::PopStyleColor();
            ImGui::EndChild();
            ImGui::PopStyleColor();
        };
        auto drawOutputCard = [&](const char* id, const char* title, char* buffer){
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::BG);
            ImGui::BeginChild(id, {0.f, panelH}, true);
            ImGui::TextUnformatted(title);
            ImGui::Spacing();
            ReadOnlyLogBox("##gmOutputText", buffer, {-FLT_MIN, panelH - 52.f});
            ImGui::EndChild();
            ImGui::PopStyleColor();
        };

        if(sideBySide){
            if(ImGui::BeginTable("##gmIO", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Output", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                switch(activeTab){
                    case ActiveTab::Stepped: drawInputCard("##gmInputStepped", "Input", hardInput, sizeof(hardInput)); break;
                    case ActiveTab::Smooth:  drawInputCard("##gmInputSmooth",  "Input", smoothInput, sizeof(smoothInput)); break;
                    case ActiveTab::Triple:  drawInputCard("##gmInputTriple",  "Input", tripleInput, sizeof(tripleInput)); break;
                }
                ImGui::TableSetColumnIndex(1);
                switch(activeTab){
                    case ActiveTab::Stepped: drawOutputCard("##gmOutputStepped", "Output", hardOutput); break;
                    case ActiveTab::Smooth:  drawOutputCard("##gmOutputSmooth",  "Output", smoothOutput); break;
                    case ActiveTab::Triple:  drawOutputCard("##gmOutputTriple",  "Output", tripleOutput); break;
                }
                ImGui::EndTable();
            }
        } else {
            switch(activeTab){
                case ActiveTab::Stepped:
                    drawInputCard("##gmInputStepped", "Input", hardInput, sizeof(hardInput));
                    ImGui::Spacing();
                    drawOutputCard("##gmOutputStepped", "Output", hardOutput);
                    break;
                case ActiveTab::Smooth:
                    drawInputCard("##gmInputSmooth", "Input", smoothInput, sizeof(smoothInput));
                    ImGui::Spacing();
                    drawOutputCard("##gmOutputSmooth", "Output", smoothOutput);
                    break;
                case ActiveTab::Triple:
                    drawInputCard("##gmInputTriple", "Input", tripleInput, sizeof(tripleInput));
                    ImGui::Spacing();
                    drawOutputCard("##gmOutputTriple", "Output", tripleOutput);
                    break;
            }
        }
    }

    void drawActions(){
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::BG);
        ImGui::BeginChild("##gmActions", {0.f, 56.f}, true);
        const float avail = ImGui::GetContentRegionAvail().x;
        const bool wide = avail >= 320.f;

        if(wide){
            if(RunButton("Generate Gradient", 170.f))
                startAction();
            ImGui::SameLine();
            const char* out = activeOutputBuffer();
            if(ImGui::Button("Copy Output", {120.f, 28.f}) && out && *out)
                ImGui::SetClipboardText(out);
        } else {
            if(RunButton("Generate Gradient", std::max(0.f, avail)))
                startAction();
            if(ImGui::Button("Copy Output", {std::max(0.f, avail), 28.f}) && activeOutputBuffer()[0] != '\0')
                ImGui::SetClipboardText(activeOutputBuffer());
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
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
        if(needsReload){
            ThemeLib().reload();
            needsReload = false;
            auto& lib = ThemeLib();
            if(!lib.themes.empty()){
                applyThemeHard(lib.themes[0]);
                applyThemeSmooth(lib.themes[0]);
                applyThemeTriple(lib.themes[0]);
                themeIdxHard = 0;
                themeIdxSmooth = 0;
                themeIdxTriple = 0;
            }
        }

        auto& lib = ThemeLib();
        std::vector<std::string> names = lib.nameList(true);

        ImGui::BeginChild("##gm", {0,0}, false);
        drawHeader(lib);
        ImGui::Spacing();
        drawModeSelector();
        ImGui::Spacing();
        drawThemePresetPanel(lib, names);
        ImGui::Spacing();
        drawPreviewPanel(lib);
        ImGui::Spacing();
        drawInputOutputSection();
        ImGui::Spacing();
        drawActions();
        drawThemeNamePopup();
        ImGui::EndChild();
    }
};

} // namespace apb::gui
