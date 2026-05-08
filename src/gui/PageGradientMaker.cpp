// src/gui/PageGradientMaker.cpp
#include "gui/App.h"
#include "imgui.h"
#include "backend/GradientMaker.h"
#include "backend/Colors.h"
#include <array>
#include <string>
#include <cstring>

namespace apb::gui {

struct GradientMakerPage : IPage {
    // ── Stepped tab state ──────────────────────────────────────
    char   steppedInput[512]  = "";
    float  steppedCols[6][3]  = {
        {1,1,1},{1,1,1},{1,1,1},{1,1,1},{1,1,1},{1,1,1}
    };
    bool   steppedSkip = true;
    std::string steppedOutput;

    // ── Smooth tab state ───────────────────────────────────────
    char   smoothInput[512] = "";
    float  smoothStart[3]   = {1,1,1};
    float  smoothEnd[3]     = {1,1,1};
    bool   smoothSkip = true;
    std::string smoothOutput;

    const char* title() override { return "Gradient Maker"; }

    void draw() override {
        if (ImGui::BeginTabBar("##gmTabs")) {
            if (ImGui::BeginTabItem("Stepped")) { drawStepped(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Smooth"))  { drawSmooth();  ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
    }

    void drawStepped() {
        ImGui::TextColored(COL_SUBTEXT, "Cycles through 6 colours in bounce order — one tag per non-space char.");
        ImGui::Spacing();

        ImGui::SetNextItemWidth(-1.f);
        ImGui::InputText("##stInput", steppedInput, sizeof(steppedInput));
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(" Input text");

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_ACCENT);
        ImGui::BeginChild("##stCols", { 0, 170.f }, true);
        for (int i = 0; i < 6; ++i) {
            char lbl[32]; snprintf(lbl, sizeof(lbl), "Colour %d", i+1);
            ImGui::TextUnformatted(lbl);
            ImGui::SameLine(110.f);
            char chipId[32]; snprintf(chipId, sizeof(chipId), "##stc%d", i);
            ColorChipButton(chipId, steppedCols[i]);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Checkbox("Skip spaces##st", &steppedSkip);
        ImGui::Spacing();

        if (ImGui::Button("Generate##st")) {
            QList<RGB> palette;
            for (int i = 0; i < 6; ++i)
                palette.append(RGB(steppedCols[i][0], steppedCols[i][1], steppedCols[i][2]));
            steppedOutput = hardGradientString(QString::fromUtf8(steppedInput), palette, steppedSkip).toStdString();
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy##st") && !steppedOutput.empty())
            ImGui::SetClipboardText(steppedOutput.c_str());

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, COL_DARK_BG);
        ImGui::InputTextMultiline("##stOut", steppedOutput.data(), steppedOutput.size() + 1,
            { -1.f, 140.f }, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();
    }

    void drawSmooth() {
        ImGui::TextColored(COL_SUBTEXT, "Lerps from Start colour to End colour across non-space chars.");
        ImGui::Spacing();

        ImGui::SetNextItemWidth(-1.f);
        ImGui::InputText("##smInput", smoothInput, sizeof(smoothInput));
        ImGui::SameLine(0,0);
        ImGui::TextUnformatted(" Input text");

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_ACCENT);
        ImGui::BeginChild("##smCols", { 0, 60.f }, true);
        ImGui::TextUnformatted("Start Colour"); ImGui::SameLine(110.f);
        ColorChipButton("##smS", smoothStart);
        ImGui::TextUnformatted("End Colour");   ImGui::SameLine(110.f);
        ColorChipButton("##smE", smoothEnd);
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Checkbox("Skip spaces##sm", &smoothSkip);
        ImGui::Spacing();

        if (ImGui::Button("Generate##sm")) {
            RGB s(smoothStart[0],smoothStart[1],smoothStart[2]);
            RGB e(smoothEnd[0],  smoothEnd[1],  smoothEnd[2]);
            smoothOutput = smoothGradientString(QString::fromUtf8(smoothInput), s, e, smoothSkip).toStdString();
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy##sm") && !smoothOutput.empty())
            ImGui::SetClipboardText(smoothOutput.c_str());

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, COL_DARK_BG);
        ImGui::InputTextMultiline("##smOut", smoothOutput.data(), smoothOutput.size() + 1,
            { -1.f, 140.f }, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();
    }

    void restoreDefaults() override {
        steppedInput[0] = '\0'; steppedOutput.clear(); steppedSkip = true;
        smoothInput[0]  = '\0'; smoothOutput.clear();  smoothSkip  = true;
        for (int i=0;i<6;++i) steppedCols[i][0]=steppedCols[i][1]=steppedCols[i][2]=1.f;
        smoothStart[0]=smoothStart[1]=smoothStart[2]=1.f;
        smoothEnd[0]=smoothEnd[1]=smoothEnd[2]=1.f;
    }
    void applySettings() override {}
};

IPage* makeGradientMakerPage() { return new GradientMakerPage; }

} // namespace apb::gui
