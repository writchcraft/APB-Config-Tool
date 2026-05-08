// src/gui/PageWeaponColour.cpp
#include "gui/App.h"
#include "imgui.h"
#include "backend/WeaponColour.h"
#include "backend/Colors.h"
#include <array>
#include <string>
#include <map>
#include <thread>
#include <atomic>
#include <cstring>

namespace apb::gui {

struct WeaponColourPage : IPage {
    // modes: 0=Mode A, 1=Mode B, 2=Single, 3=Gradient
    int  mode    = 0;
    int  fontIdx = 0;
    char filePath[1024] = "";
    std::string lastOutput;
    bool outputOpen = false;
    std::string logText;

    // per-category colour chips (preset & single modes)
    std::map<std::string, std::array<float,3>> catColors;

    // single/gradient pickers
    float singleCol[3] = {1,1,1};
    float gradStart[3] = {1,1,1};
    float gradEnd[3]   = {1,1,1};

    std::atomic<bool> running{false};
    LogQueue logQ;

    static const char* MODE_NAMES[];
    static const char* PRESET_ORDER[];
    static const char* DEFAULT_ORDER[];

    WeaponColourPage() { loadPreset(0); }

    const char* title() override { return "Weapon Colour Editor"; }

    void loadPreset(int m) {
        mode = m;
        auto cats = presetCategories();
        QMap<QString,RGB> preset = presetByIndex(m);
        for (auto& k : cats) {
            RGB c = preset.value(k, RGB(1,1,1));
            catColors[k.toStdString()] = {(float)c.r,(float)c.g,(float)c.b};
        }
        // font default
        auto fonts = availableFonts();
        if (m==0) fontIdx = fonts.indexOf("<Fonts:APBMenus_Font.APB_Helvetica_Regular_14>");
        else      fontIdx = 0;
    }

    void draw() override {
        // Drain log
        std::string line;
        while (logQ.pop(line)) logText += line + "\n";

        // ── Mode + Font row ───────────────────────────────────
        ImGui::Text("Mode"); ImGui::SameLine(80.f);
        ImGui::SetNextItemWidth(180.f);
        if (ImGui::BeginCombo("##mode", MODE_NAMES[mode])) {
            for (int i = 0; i < 4; ++i)
                if (ImGui::Selectable(MODE_NAMES[i], mode==i)) {
                    if (i==0||i==1) loadPreset(i);
                    else mode=i;
                }
            ImGui::EndCombo();
        }
        ImGui::SameLine(280.f);
        ImGui::Text("Font"); ImGui::SameLine(330.f);
        auto fonts = availableFonts();
        ImGui::SetNextItemWidth(320.f);
        const char* curFont = fontIdx < fonts.size() ? fonts[fontIdx].toUtf8().constData() : "None";
        if (ImGui::BeginCombo("##font", curFont)) {
            for (int i = 0; i < fonts.size(); ++i)
                if (ImGui::Selectable(fonts[i].toUtf8().constData(), fontIdx==i))
                    fontIdx = i;
            ImGui::EndCombo();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Category grid ─────────────────────────────────────
        bool showCatGrid = (mode==0||mode==1||mode==2);
        bool showGrad    = (mode==3);

        if (showCatGrid) {
            ImGui::TextColored(COL_YELLOW, "Weapon Categories");
            ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_ACCENT);
            ImGui::BeginChild("##cats", {0, 230.f}, true);
            auto cats = (mode==0||mode==1) ? presetCategories() : defaultCategories();
            int col=0;
            for (auto& k : cats) {
                std::string ks = k.toStdString();
                auto it = catColors.find(ks);
                if (it == catColors.end()) catColors[ks]={1,1,1};
                float* c = catColors[ks].data();
                ImGui::Text("%-16s", ks.c_str()); ImGui::SameLine(160.f);
                std::string cid = "##cc_"+ks;
                ColorChipButton(cid.c_str(), c);
                if (++col % 2 == 0) {} else { ImGui::SameLine(280.f); }
                if (col%2 == 0) col=0;
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        if (showGrad) {
            ImGui::Text("Gradient Start:"); ImGui::SameLine(160.f);
            ColorChipButton("##gs", gradStart);
            ImGui::Text("Gradient End:");   ImGui::SameLine(160.f);
            ColorChipButton("##ge", gradEnd);
        }

        ImGui::Spacing();

        // ── File row ──────────────────────────────────────────
        ImGui::Text("GER File:"); ImGui::SameLine(80.f);
        ImGui::SetNextItemWidth(400.f);
        ImGui::InputText("##fp", filePath, sizeof(filePath));
        ImGui::SameLine();
        if (ImGui::Button("Browse##wc")) {
            // No native dialog without Qt; show hint
            ImGui::OpenPopup("##browseHint");
        }
        if (ImGui::BeginPopup("##browseHint")) {
            ImGui::TextColored(COL_YELLOW, "Paste the full file path above.");
            ImGui::EndPopup();
        }

        ImGui::Spacing();

        bool busy = running.load();
        if (busy) ImGui::BeginDisabled();
        if (ImGui::Button("Apply Colours##wc")) runProcess();
        if (busy) ImGui::EndDisabled();

        if (outputOpen) {
            ImGui::SameLine();
            if (ImGui::Button("Open Output##wc"))
                ShellExecuteA(nullptr,"open",lastOutput.c_str(),nullptr,nullptr,SW_SHOW);
        }

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, COL_DARK_BG);
        float logH = ImGui::GetContentRegionAvail().y - 4.f;
        ImGui::InputTextMultiline("##wcLog", logText.data(), logText.size()+1,
            {-1.f, logH}, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();
    }

    void runProcess() {
        if (running.load()) return;
        std::string path(filePath);
        if (path.empty()) { logText = "No file selected.\n"; return; }

        QMap<QString,RGB> colorMap;
        auto cats = (mode==0||mode==1) ? presetCategories() : defaultCategories();
        for (auto& k : cats) {
            auto it = catColors.find(k.toStdString());
            if (it!=catColors.end())
                colorMap[k] = RGB(it->second[0],it->second[1],it->second[2]);
        }

        ColourMode cm = ColourMode::PRESET;
        if (mode==2) cm = ColourMode::SINGLE;
        if (mode==3) cm = ColourMode::GRADIENT;

        auto fonts = availableFonts();
        QString font = (fontIdx<fonts.size()) ? fonts[fontIdx] : "None";
        RGB single(singleCol[0],singleCol[1],singleCol[2]);
        RGB gs(gradStart[0],gradStart[1],gradStart[2]);
        RGB ge(gradEnd[0],  gradEnd[1],  gradEnd[2]);

        logText.clear(); outputOpen=false; running=true;
        std::thread([=]() mutable {
            try {
                auto res = applyColourToGerFile(QString::fromStdString(path), colorMap,
                    font, cm, single, gs, ge,
                    [this](const QString& m){ logQ.push(m.toStdString()); });
                logQ.push("Done: " + std::to_string(res.newlyColoured) + " newly coloured → " + res.outputPath.toStdString());
                lastOutput = res.outputPath.toStdString();
                outputOpen = true;
            } catch(std::exception& e) {
                logQ.push(std::string("Error: ")+e.what());
            }
            running = false;
        }).detach();
    }

    void restoreDefaults() override {
        mode=0; fontIdx=0; filePath[0]='\0';
        logText.clear(); outputOpen=false;
        loadPreset(0);
    }
    void applySettings() override {}
};

const char* WeaponColourPage::MODE_NAMES[] = { "Mode A","Mode B","Single Colour","Gradient" };
const char* WeaponColourPage::PRESET_ORDER[] = {"Rifle","SMG","AssaultRifle","SniperRifle","Shotgun","LMG","LTL","Explosive","Pistol","Grenade",nullptr};
const char* WeaponColourPage::DEFAULT_ORDER[] = {"AssaultRifle","Rifle","SMG","Shotgun","SniperRifle","LMG","Explosive","LTL","Pistol","Grenade",nullptr};

IPage* makeWeaponColourPage() { return new WeaponColourPage; }

} // namespace apb::gui
