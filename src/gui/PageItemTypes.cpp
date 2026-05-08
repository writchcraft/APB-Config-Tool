// src/gui/PageItemTypes.cpp
// VehicleItemTypes + WeaponItemTypes pages
#include "gui/App.h"
#include "imgui.h"
#include "backend/VehicleItemTypes.h"
#include "backend/WeaponItemTypes.h"
#include "backend/Formatter.h"
#include "backend/Colors.h"
#include <string>
#include <thread>
#include <atomic>
#include <array>
#include <vector>
#include <cstring>
#include <algorithm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

namespace apb::gui {

// ─────────────────────────────────────────────────────────────
//  Shared mode combo + colour pickers (used by both pages)
// ─────────────────────────────────────────────────────────────
static const char* ITEM_MODES[] = {
    "Vanilla",
    "APB.DB Stats",
    "APB.DB Stats - Single Colour",
    "APB.DB Stats - Gradient",
    nullptr
};

static Scheme modeToScheme(int mode) {
    switch(mode) {
        case 2: return Scheme::SINGLE;
        case 3: return Scheme::GRADIENT;
        default: return Scheme::CLEAR;
    }
}

static void syncModeColours(int mode, float col1[3], float col2[3]) {
    if (mode < 2) {
        col1[0]=col1[1]=col1[2]=1.f;
        col2[0]=col2[1]=col2[2]=1.f;
    }
}

// Gradient HTML-like text helper for preview
static std::string gradientLineText(const char* key, const char* val,
    float c1[3], float c2[3])
{
    std::string out;
    std::string ks(key); ks += ":";
    int n = std::max(1,(int)ks.size()-1);
    for (int i=0;i<(int)ks.size();++i) {
        float t = (float)i/n;
        // just produce coloured text tag for preview – ImGui doesn't support inline colours,
        // so we use the APB tag format as a plain text preview
        float r = c1[0]+(c2[0]-c1[0])*t;
        float g = c1[1]+(c2[1]-c1[1])*t;
        float b = c1[2]+(c2[2]-c1[2])*t;
        char buf[64];
        snprintf(buf,sizeof(buf),"<Color:R=%.3f G=%.3f B=%.3f>%c<Color:/>",r,g,b,ks[i]);
        out += buf;
    }
    out += " "; out += val;
    return out;
}

// ═══════════════════════════════════════════════════════════════
//  VehicleItemTypes page
// ═══════════════════════════════════════════════════════════════
struct VehicleItemTypesPage : IPage {
    char   intPath[1024] = "";
    char   outPath[1024] = "";
    int    workers    = 8;
    int    modeIdx    = 0;
    float  col1[3]    = {1,1,1};
    float  col2[3]    = {1,1,1};
    std::string logText, statusText="Idle", lastOut;
    bool   outReady = false;
    int    progressDone=0, progressTotal=1;
    std::atomic<bool> running{false};
    LogQueue logQ;

    // Sample stats for preview
    struct SampleCard { const char* name; std::vector<std::pair<const char*,const char*>> stats; };
    static const SampleCard CARDS[4];

    const char* title() override { return "VehicleItemTypes"; }

    void draw() override {
        std::string line; while(logQ.pop(line)) logText+=line+"\n";

        ImGui::TextColored(COL_YELLOW, "VehicleItemTypes Generator");
        ImGui::Spacing();

        // File inputs
        ImGui::Text("INT File"); ImGui::SameLine(100.f);
        ImGui::SetNextItemWidth(380.f); ImGui::InputText("##vint",intPath,sizeof(intPath));
        ImGui::Text("Output");   ImGui::SameLine(100.f);
        ImGui::SetNextItemWidth(380.f); ImGui::InputText("##vout",outPath,sizeof(outPath));
        ImGui::Text("Workers");  ImGui::SameLine(100.f);
        ImGui::SetNextItemWidth(70.f);  ImGui::InputInt("##vw",&workers);
        workers=std::max(1,std::min(64,workers));

        ImGui::Text("Style");    ImGui::SameLine(100.f);
        ImGui::SetNextItemWidth(280.f);
        int prevMode = modeIdx;
        if (ImGui::BeginCombo("##vmode", ITEM_MODES[modeIdx])) {
            for (int i=0; ITEM_MODES[i]; ++i)
                if (ImGui::Selectable(ITEM_MODES[i], modeIdx==i)) modeIdx=i;
            ImGui::EndCombo();
        }
        if (prevMode!=modeIdx) syncModeColours(modeIdx, col1, col2);

        // Colour pickers
        if (modeIdx==2) { // single
            ImGui::SameLine(400.f); ImGui::Text("Colour:"); ImGui::SameLine();
            ColorChipButton("##vc1", col1);
        }
        if (modeIdx==3) { // gradient
            ImGui::SameLine(400.f); ImGui::Text("Grad:"); ImGui::SameLine();
            ColorChipButton("##vc1g", col1); ImGui::SameLine();
            ColorChipButton("##vc2g", col2);
        }

        ImGui::Spacing();

        // ── Preview cards ─────────────────────────────────────
        ImGui::TextColored(COL_SUBTEXT, "Preview  (sample stats)");
        ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_DARK_BG);
        float cardW = (ImGui::GetContentRegionAvail().x - 24.f) * 0.5f;
        for (int ci=0; ci<4; ++ci) {
            if (ci==2) {} // new "row" handled by same-line logic
            ImGui::BeginChild(CARDS[ci].name, {cardW, 140.f}, true);
            ImGui::TextColored(COL_YELLOW, "%s", CARDS[ci].name);
            for (auto& [k,v] : CARDS[ci].stats) {
                if (modeIdx==3) {
                    ImGui::TextDisabled("%s", gradientLineText(k,v,col1,col2).c_str());
                } else if (modeIdx==2) {
                    ImGui::PushStyleColor(ImGuiCol_Text, {col1[0],col1[1],col1[2],1.f});
                    ImGui::Text("%s:", k); ImGui::SameLine(); ImGui::Text("%s",v);
                    ImGui::PopStyleColor();
                } else {
                    ImGui::Text("%s: %s", k, v);
                }
            }
            ImGui::EndChild();
            if (ci==0||ci==2) ImGui::SameLine();
        }
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // Run / status
        bool busy = running.load();
        if (busy) ImGui::BeginDisabled();
        if (ImGui::Button("Run##veh")) startRun();
        if (busy) ImGui::EndDisabled();
        if (outReady) { ImGui::SameLine();
            if (ImGui::Button("Open Output##veh"))
                ShellExecuteA(nullptr,"open",lastOut.c_str(),nullptr,nullptr,SW_SHOW); }

        ImGui::SameLine(200.f);
        float pct = progressTotal ? (float)progressDone/progressTotal : 0.f;
        ImGui::ProgressBar(pct, {200.f, 0.f}); ImGui::SameLine();
        ImGui::Text("%s", statusText.c_str());

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, COL_DARK_BG);
        float logH = ImGui::GetContentRegionAvail().y - 4.f;
        ImGui::InputTextMultiline("##vlog", logText.data(), logText.size()+1,
            {-1.f, std::max(60.f,logH)}, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();
    }

    void startRun() {
        if (running.load()) return;
        std::string ip(intPath), op(outPath);
        if(ip.empty()){statusText="Select INT file."; return;}
        logText.clear(); outReady=false; running=true;
        statusText="Working…"; progressDone=0; progressTotal=1;
        Scheme sc = modeToScheme(modeIdx);
        RGB single(col1[0],col1[1],col1[2]);
        RGB gs(col1[0],col1[1],col1[2]), ge(col2[0],col2[1],col2[2]);
        std::thread([=]() mutable {
            try {
                generateVehicleDescriptionsFile(
                    QString::fromStdString(ip),
                    QString::fromStdString(op),
                    sc, single, gs, ge, 15, workers,
                    [this](int d,int t,const QString& k){
                        progressDone=d; progressTotal=t;
                        statusText=k.toStdString();
                    });
                lastOut = op.empty() ?
                    (std::string(intPath).substr(0,std::string(intPath).rfind('\\'))+"\\VehicleItemTypes.GER")
                    : op;
                outReady=true; statusText="Done.";
            } catch(std::exception& e){ logQ.push(std::string("Error: ")+e.what()); statusText="Failed."; }
            running=false;
        }).detach();
    }

    void restoreDefaults() override {
        intPath[0]=outPath[0]='\0'; workers=8; modeIdx=0;
        col1[0]=col1[1]=col1[2]=1.f; col2[0]=col2[1]=col2[2]=1.f;
        logText.clear(); statusText="Idle"; outReady=false;
    }
    void applySettings() override {}
};

const VehicleItemTypesPage::SampleCard VehicleItemTypesPage::CARDS[4] = {
    { "Han Coywolf CR4",        {{"Max Health","990"},{"Max Speed","20.1 m/s"},{"Cargo","5"},{"Weight","2.4"},{"Explode Dmg","1400"},{"Explode Rad","750 cm"}} },
    { "Joker Vegas G24 4x4",    {{"Max Health","1150"},{"Max Speed","22 m/s"},{"Cargo","5"},{"Weight","4.0"},{"Explode Dmg","1400"},{"Explode Rad","750 cm"}} },
    { "Nulander Nomad Q134",    {{"Max Health","1500"},{"Max Speed","20.9 m/s"},{"Cargo","15"},{"Weight","5.5"},{"Explode Dmg","2000"},{"Explode Rad","850 cm"}} },
    { "Sungnyemun Mirage S-24", {{"Max Health","1050"},{"Max Speed","22.5 m/s"},{"Cargo","5"},{"Weight","2.4"},{"Explode Dmg","1400"},{"Explode Rad","750 cm"}} },
};

IPage* makeVehicleItemTypesPage() { return new VehicleItemTypesPage; }

// ═══════════════════════════════════════════════════════════════
//  WeaponItemTypes page
// ═══════════════════════════════════════════════════════════════
struct WeaponItemTypesPage : IPage {
    char   intPath[1024] = "";
    char   outPath[1024] = "";
    int    workers    = 8;
    int    modeIdx    = 0;
    float  col1[3]    = {1,1,1};
    float  col2[3]    = {1,1,1};
    std::string logText, statusText="Idle", lastOut;
    bool   outReady = false;
    int    progressDone=0, progressTotal=1;
    std::atomic<bool> running{false};
    LogQueue logQ;

    struct SampleCard { const char* name; std::vector<std::pair<const char*,const char*>> stats; };
    static const SampleCard CARDS[4];

    const char* title() override { return "WeaponItemTypes"; }

    void draw() override {
        std::string line; while(logQ.pop(line)) logText+=line+"\n";

        ImGui::TextColored(COL_YELLOW, "WeaponItemTypes Generator");
        ImGui::Spacing();

        ImGui::Text("INT File"); ImGui::SameLine(100.f);
        ImGui::SetNextItemWidth(380.f); ImGui::InputText("##wint",intPath,sizeof(intPath));
        ImGui::Text("Output");   ImGui::SameLine(100.f);
        ImGui::SetNextItemWidth(380.f); ImGui::InputText("##wout",outPath,sizeof(outPath));
        ImGui::Text("Workers");  ImGui::SameLine(100.f);
        ImGui::SetNextItemWidth(70.f);  ImGui::InputInt("##ww",&workers);
        workers=std::max(1,std::min(64,workers));

        ImGui::Text("Style");    ImGui::SameLine(100.f);
        ImGui::SetNextItemWidth(280.f);
        int prevMode = modeIdx;
        if (ImGui::BeginCombo("##wmode", ITEM_MODES[modeIdx])) {
            for (int i=0; ITEM_MODES[i]; ++i)
                if (ImGui::Selectable(ITEM_MODES[i], modeIdx==i)) modeIdx=i;
            ImGui::EndCombo();
        }
        if (prevMode!=modeIdx) syncModeColours(modeIdx, col1, col2);

        if (modeIdx==2) {
            ImGui::SameLine(400.f); ImGui::Text("Colour:"); ImGui::SameLine();
            ColorChipButton("##wc1", col1);
        }
        if (modeIdx==3) {
            ImGui::SameLine(400.f); ImGui::Text("Grad:"); ImGui::SameLine();
            ColorChipButton("##wc1g", col1); ImGui::SameLine();
            ColorChipButton("##wc2g", col2);
        }

        ImGui::Spacing();
        ImGui::TextColored(COL_SUBTEXT, "Preview  (sample stats)");
        ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_DARK_BG);
        float cardW = (ImGui::GetContentRegionAvail().x - 24.f) * 0.5f;
        for (int ci=0; ci<4; ++ci) {
            ImGui::BeginChild(CARDS[ci].name, {cardW, 140.f}, true);
            ImGui::TextColored(COL_YELLOW, "%s", CARDS[ci].name);
            for (auto& [k,v] : CARDS[ci].stats) {
                if (modeIdx==3) {
                    ImGui::TextDisabled("%s", gradientLineText(k,v,col1,col2).c_str());
                } else if (modeIdx==2) {
                    ImGui::PushStyleColor(ImGuiCol_Text, {col1[0],col1[1],col1[2],1.f});
                    ImGui::Text("%s:", k); ImGui::SameLine(); ImGui::Text("%s",v);
                    ImGui::PopStyleColor();
                } else {
                    ImGui::Text("%s: %s", k, v);
                }
            }
            ImGui::EndChild();
            if (ci==0||ci==2) ImGui::SameLine();
        }
        ImGui::PopStyleColor();

        ImGui::Spacing();
        bool busy = running.load();
        if (busy) ImGui::BeginDisabled();
        if (ImGui::Button("Run##wpn")) startRun();
        if (busy) ImGui::EndDisabled();
        if (outReady) { ImGui::SameLine();
            if (ImGui::Button("Open Output##wpn"))
                ShellExecuteA(nullptr,"open",lastOut.c_str(),nullptr,nullptr,SW_SHOW); }

        ImGui::SameLine(200.f);
        float pct = progressTotal ? (float)progressDone/progressTotal : 0.f;
        ImGui::ProgressBar(pct, {200.f,0.f}); ImGui::SameLine();
        ImGui::Text("%s", statusText.c_str());

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, COL_DARK_BG);
        float logH = ImGui::GetContentRegionAvail().y - 4.f;
        ImGui::InputTextMultiline("##wlog", logText.data(), logText.size()+1,
            {-1.f, std::max(60.f,logH)}, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();
    }

    void startRun() {
        if (running.load()) return;
        std::string ip(intPath), op(outPath);
        if(ip.empty()){statusText="Select INT file."; return;}
        logText.clear(); outReady=false; running=true;
        statusText="Working…"; progressDone=0; progressTotal=1;
        Scheme sc = modeToScheme(modeIdx);
        RGB single(col1[0],col1[1],col1[2]);
        RGB gs(col1[0],col1[1],col1[2]), ge(col2[0],col2[1],col2[2]);
        std::thread([=]() mutable {
            try {
                generateWeaponDescriptionsFile(
                    QString::fromStdString(ip),
                    QString::fromStdString(op),
                    sc, single, gs, ge, 15, workers,
                    [this](int d,int t,const QString& k){
                        progressDone=d; progressTotal=t;
                        statusText=k.toStdString();
                    });
                lastOut = op; outReady=true; statusText="Done.";
            } catch(std::exception& e){ logQ.push(std::string("Error: ")+e.what()); statusText="Failed."; }
            running=false;
        }).detach();
    }

    void restoreDefaults() override {
        intPath[0]=outPath[0]='\0'; workers=8; modeIdx=0;
        col1[0]=col1[1]=col1[2]=1.f; col2[0]=col2[1]=col2[2]=1.f;
        logText.clear(); statusText="Idle"; outReady=false;
    }
    void applySettings() override {}
};

const WeaponItemTypesPage::SampleCard WeaponItemTypesPage::CARDS[4] = {
    { "UL-3 'Jersey Devil'", {{"TTK","0.756s"},{"STK","13"},{"Dmg","80"},{"Stam Dmg","20"},{"Range","30m"},{"Fire Int","0.063s"}} },
    { "Stabba - NL9",        {{"TTS","1.56s"},{"STS","3"},{"Dmg","200"},{"Stam Dmg","400"},{"Range","50m"},{"Fire Int","0.78s"}} },
    { "OSMAW",               {{"TTK","1.75s"},{"STK","1"},{"Max Dmg","1000"},{"Wind Up","1.75s"},{"Reload","2.25s"}} },
    { "Frag Grenade",        {{"Max Dmg","750"},{"Max Stam","375"},{"Radius","700cm"},{"Fuse","4s"},{"Speed","15.5m/s"}} },
};

IPage* makeWeaponItemTypesPage() { return new WeaponItemTypesPage; }

} // namespace apb::gui
