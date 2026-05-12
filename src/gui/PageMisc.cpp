// src/gui/PageMisc.cpp
// InventoryItemTypes + ArmasScrape + Credits + Localization pages
#include "gui/App.h"
#include "imgui.h"
#include "backend/InventoryItemTypes.h"
#include "backend/ArmasScraper.h"
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <cstring>

// Windows shell for "open file"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

namespace apb::gui {

// ═══════════════════════════════════════════════════════════════
//  InventoryItemTypes page
// ═══════════════════════════════════════════════════════════════
struct InventoryItemTypesPage : IPage {
    char gerPath[1024]  = "";
    char intPath[1024]  = "";
    char outPath[1024]  = "";
    bool appendRemaining = false;
    bool writeReport     = false;
    std::string logText;
    std::string lastOut;
    bool outReady = false;
    std::atomic<bool> running{false};
    LogQueue logQ;

    const char* title() override { return "InventoryItemTypes"; }

    void draw() override {
        std::string line;
        while (logQ.pop(line)) logText += line + "\n";

        ImGui::TextColored(COL_YELLOW, "InventoryItemTypes  –  Gap-aware GER ← INT merge");
        ImGui::Spacing();

        drawFilePicker("Your GER file",     gerPath, sizeof(gerPath));
        drawFilePicker("Default INT file",  intPath, sizeof(intPath));
        drawFilePicker("Output (UTF-16)",   outPath, sizeof(outPath));

        ImGui::Spacing();
        ImGui::Checkbox("Append INT-only keys at end##inv",     &appendRemaining);
        ImGui::Checkbox("Also save _REPORT.txt next to output##inv", &writeReport);
        ImGui::Spacing();

        bool busy = running.load();
        if (busy) ImGui::BeginDisabled();
        if (ImGui::Button("Run##inv")) startRun();
        if (busy) ImGui::EndDisabled();

        if (outReady) {
            ImGui::SameLine();
            if (ImGui::Button("Open Output##inv"))
                ShellExecuteA(nullptr,"open",lastOut.c_str(),nullptr,nullptr,SW_SHOW);
        }

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, COL_DARK_BG);
        float logH = ImGui::GetContentRegionAvail().y - 4.f;
        ImGui::InputTextMultiline("##invLog", logText.data(), logText.size()+1,
            {-1.f, logH}, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();
    }

    static void drawFilePicker(const char* label, char* buf, size_t sz) {
        ImGui::Text("%-22s", label); ImGui::SameLine(200.f);
        ImGui::SetNextItemWidth(400.f);
        char id[64]; snprintf(id,sizeof(id),"##fp_%s",label);
        ImGui::InputText(id, buf, sz);
    }

    void startRun() {
        if (running.load()) return;
        std::string ger(gerPath), intF(intPath), out(outPath);
        if (ger.empty()||intF.empty()){ logText="Both GER and INT paths required.\n"; return; }
        logText.clear(); outReady=false; running=true;
        std::thread([=]() mutable {
            try {
                auto res = mergeInventoryItemTypes(
                    QString::fromStdString(ger),
                    QString::fromStdString(intF),
                    QString::fromStdString(out),
                    appendRemaining, writeReport);
                logQ.push(res.reportText.isEmpty() ? "No new entries added." : res.reportText.toStdString());
                lastOut = res.outputPath.toStdString();
                outReady = true;
            } catch(std::exception& e){ logQ.push(std::string("Error: ")+e.what()); }
            running = false;
        }).detach();
    }

    void restoreDefaults() override {
        gerPath[0]=intPath[0]=outPath[0]='\0';
        appendRemaining=writeReport=false;
        logText.clear(); outReady=false;
    }
    void applySettings() override {}
};

IPage* makeInventoryItemTypesPage() { return new InventoryItemTypesPage; }

// ═══════════════════════════════════════════════════════════════
//  ArmasScrape page
// ═══════════════════════════════════════════════════════════════
struct HitRow { std::string title, url; };

struct ArmasScrapePage : IPage {
    int startId  = 0;
    int endId    = 25000;
    int threads  = 32;
    char baseUrl[512] = "https://www.gamersfirst.com/marketplace/ingame/product_details.php?storetype=g1c&gameID=20&catID=62&subcatID=258&productId=";
    char outPath[1024] = "";
    std::string logText;
    std::string statusText = "Idle";
    int  progressDone=0, progressTotal=1;
    std::vector<HitRow> hits;
    std::atomic<bool> running{false};
    std::atomic<bool> stopFlag{false};
    std::string lastOut;
    bool outReady=false;
    std::mutex hitsMtx;
    LogQueue logQ;
    ArmasScraper* scraperPtr = nullptr;

    const char* title() override { return "ARMAS Scanner"; }

    void draw() override {
        // Drain
        std::string line; while(logQ.pop(line)) logText += line+"\n";

        ImGui::TextColored(COL_YELLOW, "ARMAS Product ID Scanner");
        ImGui::Spacing();

        ImGui::Text("Start ID");  ImGui::SameLine(120.f); ImGui::SetNextItemWidth(100.f); ImGui::InputInt("##sid",&startId);
        ImGui::Text("End ID");    ImGui::SameLine(120.f); ImGui::SetNextItemWidth(100.f); ImGui::InputInt("##eid",&endId);
        ImGui::Text("Threads");   ImGui::SameLine(120.f); ImGui::SetNextItemWidth(80.f);  ImGui::InputInt("##thr",&threads);
        ImGui::Text("Base URL");  ImGui::SameLine(120.f); ImGui::SetNextItemWidth(-1.f);  ImGui::InputText("##burl",baseUrl,sizeof(baseUrl));
        ImGui::Text("Output");    ImGui::SameLine(120.f); ImGui::SetNextItemWidth(-1.f);  ImGui::InputText("##aout",outPath,sizeof(outPath));

        ImGui::Spacing();

        bool busy = running.load();
        if (!busy) {
            if (ImGui::Button("Start##armas")) startScrape();
        } else {
            if (ImGui::Button("Stop##armas")) stopScrape();
        }

        if (outReady) {
            ImGui::SameLine();
            if (ImGui::Button("Open Output##armas"))
                ShellExecuteA(nullptr,"open",lastOut.c_str(),nullptr,nullptr,SW_SHOW);
        }

        // Progress
        ImGui::Spacing();
        float pct = progressTotal ? (float)progressDone/progressTotal : 0.f;
        ImGui::ProgressBar(pct, {-1.f,0.f});
        ImGui::Text("%s", statusText.c_str());

        // Hits table
        ImGui::Spacing();
        ImGui::TextColored(COL_YELLOW, "Found Items  (%d)", (int)hits.size());
        ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_DARK_BG);
        if (ImGui::BeginTable("##hits", 2,
            ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,
            {0.f, 200.f})) {
            ImGui::TableSetupColumn("Name"); ImGui::TableSetupColumn("URL");
            ImGui::TableHeadersRow();
            std::lock_guard<std::mutex> lk(hitsMtx);
            for (auto& h : hits) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(h.title.c_str());
                ImGui::TableSetColumnIndex(1);
                if (ImGui::Selectable(h.url.c_str(), false))
                    ShellExecuteA(nullptr,"open",h.url.c_str(),nullptr,nullptr,SW_SHOW);
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, COL_DARK_BG);
        float logH = ImGui::GetContentRegionAvail().y - 4.f;
        ImGui::InputTextMultiline("##arLog", logText.data(), logText.size()+1,
            {-1.f, std::max(60.f, logH)}, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();
    }

    void startScrape() {
        if (running.load()) return;
        logText.clear(); { std::lock_guard<std::mutex> lk(hitsMtx); hits.clear(); }
        outReady=false; progressDone=0; statusText="Starting…"; stopFlag=false; running=true;

        ScrapeConfig cfg;
        cfg.startId=startId; cfg.endId=endId; cfg.threads=threads;
        cfg.outPath=QString::fromUtf8(outPath);
        cfg.baseUrl=QString::fromUtf8(baseUrl);
        cfg.includeTitles=true;

        std::thread([=]() mutable {
            ArmasScraper sc(cfg, nullptr);
            scraperPtr = &sc;
            QString out = sc.run(
                [this](int d,int t,const QString& m){
                    progressDone=d; progressTotal=t;
                    statusText=m.toStdString();
                },
                [this](int,const QString& url,const QString& ttl){
                    std::lock_guard<std::mutex> lk(hitsMtx);
                    hits.push_back({ttl.toStdString(), url.toStdString()});
                });
            lastOut = out.toStdString(); outReady=!lastOut.empty();
            statusText = "Done."; running=false; scraperPtr=nullptr;
        }).detach();
    }

    void stopScrape() {
        if (scraperPtr) scraperPtr->stop();
        statusText="Stopping…";
    }

    void restoreDefaults() override {
        startId=0; endId=25000; threads=32;
        logText.clear(); statusText="Idle";
        progressDone=0; progressTotal=1; outReady=false;
        { std::lock_guard<std::mutex> lk(hitsMtx); hits.clear(); }
    }
    void applySettings() override {}
};

IPage* makeArmasScrapePage() { return new ArmasScrapePage; }

// ═══════════════════════════════════════════════════════════════
//  Credits page
// ═══════════════════════════════════════════════════════════════
struct CreditsPage : IPage {
    const char* title() override { return "Credits"; }
    void draw() override {
        ImGui::TextColored(COL_YELLOW, "APB Config Tool");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "An open-source localisation and configuration utility for "
            "All Points Bulletin: Reloaded.\n\n"
            "Provides weapon stat generation, vehicle stat generation, "
            "inventory merging, gradient tools, and ARMAS product scanning.");
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        auto row = [](const char* k, const char* v){
            ImGui::PushStyleColor(ImGuiCol_Text, COL_YELLOW);
            ImGui::Text("%-18s", k);
            ImGui::PopStyleColor();
            ImGui::SameLine(160.f);
            ImGui::TextWrapped("%s", v);
        };
        row("Author",       "writch");
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        row("Credits",      "Mewpri - original creator of the ItemTypes stat maker");
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextColored(COL_SUBTEXT,
            "Not affiliated with Little Orbit or GamersFirst.\n"
            "APB: Reloaded\xe2\x84\xa2 is a trademark of Little Orbit LLC.");
    }
    void restoreDefaults() override {}
    void applySettings()   override {}
};
IPage* makeCreditsPage() { return new CreditsPage; }

// ═══════════════════════════════════════════════════════════════
//  Localization page
// ═══════════════════════════════════════════════════════════════
static const char* SYMBOLS_TEXT =
    "~ \xe2\x80\xa2 \xc3\x97 \xc2\xa1 \xc2\xa2 \xc2\xa3 \xc2\xa4 \xc2\xa5 \xc2\xa6 \xc2\xa7 ...\n\n"
    "Newline character: [\xe2\x86\xb5]\n"
    "Blank character:   [ ]\n\n"
    "Some files use \\n for newlines instead of the dedicated character.";

static const char* FONTS_TEXT =
    "<Fonts:APBMenus_Font.APB_Helvetica_Bold_11>\n"
    "<Fonts:APBMenus_Font.APB_Helvetica_Bold_13>\n"
    "<Fonts:APBMenus_Font.APB_Helvetica_Bold_14>\n"
    "<Fonts:APBMenus_Font.APB_Helvetica_Bold_24>\n"
    "<Fonts:APBMenus_Font.APB_Helvetica_Bold_32>\n"
    "<Fonts:APBMenus_Font.APB_Helvetica_Regular_11>\n"
    "<Fonts:APBMenus_Font.APB_Helvetica_Regular_12>\n"
    "<Fonts:APBMenus_Font.APB_Helvetica_Regular_14>\n"
    "<Fonts:APBMenus_Font.APB_Helvetica_Regular_16>\n"
    "<Fonts:APBMenus_Font.APB_Helvetica_Regular_28>\n"
    "<Fonts:APBMenus_Font.APB_HUD_AmmoCounter>\n"
    "<Fonts:EngineFonts.TinyFont>\n"
    "<Fonts:EngineFonts.SmallFont>\n"
    "<Fonts:EngineFonts.MediumFont>\n"
    "<Fonts:EngineFonts.LargeFont>\n\n"
    "NOTE: Font tags do not need closing tags. Do not use <Fonts:/>.";

static const char* COLOR_INTRO =
    "Use <col:TagName>text</col> or <Color:R=1 G=1 B=1>text<Color:/>\n\n"
    "Common named tags: White, Black, Red, Green, Blue, Yellow, Orange,\n"
    "Purple, Cyan, Grey, Chat_Say, Chat_Team, Chat_Combat, HUDMessage_Error …\n\n"
    "Convert a named tag to its <Color:R G B> equivalent using the picker below.";

struct LocalizationPage : IPage {
    const char* title() override { return "Localization Resources"; }

    void draw() override {
        if (ImGui::BeginTabBar("##locTabs")) {
            if (ImGui::BeginTabItem("Symbols")) {
                ImGui::PushStyleColor(ImGuiCol_FrameBg, COL_DARK_BG);
                ImGui::InputTextMultiline("##sym", const_cast<char*>(SYMBOLS_TEXT),
                    strlen(SYMBOLS_TEXT)+1, {-1.f,-1.f}, ImGuiInputTextFlags_ReadOnly);
                ImGui::PopStyleColor();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Fonts")) {
                ImGui::PushStyleColor(ImGuiCol_FrameBg, COL_DARK_BG);
                ImGui::InputTextMultiline("##fnt", const_cast<char*>(FONTS_TEXT),
                    strlen(FONTS_TEXT)+1, {-1.f,-1.f}, ImGuiInputTextFlags_ReadOnly);
                ImGui::PopStyleColor();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Colour Codes")) {
                ImGui::PushStyleColor(ImGuiCol_FrameBg, COL_DARK_BG);
                ImGui::InputTextMultiline("##colIntro", const_cast<char*>(COLOR_INTRO),
                    strlen(COLOR_INTRO)+1, {-1.f, 100.f}, ImGuiInputTextFlags_ReadOnly);
                ImGui::PopStyleColor();

                ImGui::Spacing();
                ImGui::TextColored(COL_YELLOW, "Tip:"); ImGui::SameLine();
                ImGui::TextWrapped("To turn a hex colour into an APB tag, pick a colour below and copy the generated tag.");
                ImGui::Spacing();

                static float col[3] = {1,1,1};
                ImGui::Text("Preview colour:"); ImGui::SameLine(150.f);
                if (ImGui::ColorEdit3("##previewcol", col, ImGuiColorEditFlags_DisplayHex)) {}
                ImGui::Spacing();

                char colTag[128];
                std::snprintf(colTag, sizeof(colTag), "<Color:R=%s G=%s B=%s>",
                    fmtF(col[0]).c_str(), fmtF(col[1]).c_str(), fmtF(col[2]).c_str());
                ImGui::Text("APB Colour tag:"); ImGui::SameLine(150.f);
                ImGui::SetNextItemWidth(380.f);
                ImGui::InputText("##ctag", colTag, sizeof(colTag), ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine();
                if (ImGui::Button("Copy##loccol"))
                    ImGui::SetClipboardText(colTag);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    void restoreDefaults() override {}
    void applySettings()   override {}
};
IPage* makeLocalizationPage() { return new LocalizationPage; }

} // namespace apb::gui
