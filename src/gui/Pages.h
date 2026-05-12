#pragma once
#include "App.h"
#include "backend/InventoryItemTypes.h"
#include "backend/PremadeConfigs.h"
#include "backend/Contacts.h"
#include "backend/PlayerRoles.h"
#include "backend/WeaponColour.h"
#include "backend/WeaponItemTypes.h"
#include "backend/VehicleItemTypes.h"
#include "backend/ArmasScraper.h"
#include "backend/AppDirs.h"
#include "backend/GradientMaker.h"
#include "backend/ImageLoader.h"
#include "backend/ThemeLibrary.h"
#include "../resource.h"
#include <d3d11.h>
#include <fstream>
#include <filesystem>
#include <array>
#include <cmath>
#include <iomanip>
#include <map>
#include <regex>

// D3D device from main.cpp
extern ID3D11Device* g_dev;

namespace apb::gui {

// ══════════════════════════════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════════════════════════════

// Draw a colour-coded stat line using ImDrawList over a screen rect
// Returns the y position after the last line drawn
// Lerp two ImU32 colours by t in [0,1]
static ImU32 LerpCol(ImU32 a, ImU32 b, float t){
    float ar=(a>>0 &0xff)/255.f, ag=(a>>8 &0xff)/255.f,
          ab=(a>>16&0xff)/255.f, aa=(a>>24&0xff)/255.f;
    float br=(b>>0 &0xff)/255.f, bg=(b>>8 &0xff)/255.f,
          bb=(b>>16&0xff)/255.f, ba=(b>>24&0xff)/255.f;
    auto lp=[](float x,float y,float t){ return x+(y-x)*t; };
    return IM_COL32((int)(lp(ar,br,t)*255),(int)(lp(ag,bg,t)*255),
                    (int)(lp(ab,bb,t)*255),(int)(lp(aa,ba,t)*255));
}

struct PreviewTextGradient {
    ImU32 start;
    ImU32 middle;
    ImU32 end;
    bool triple;
};

static ImU32 SamplePreviewTextGradient(const PreviewTextGradient& gradient, float t){
    if(!gradient.triple)
        return LerpCol(gradient.start, gradient.end, t);
    if(t <= 0.5f)
        return LerpCol(gradient.start, gradient.middle, t * 2.f);
    return LerpCol(gradient.middle, gradient.end, (t - 0.5f) * 2.f);
}

// Draw stat lines with left-to-right gradient across each key label
static float DrawStatLines(
    ImDrawList* dl,
    ImVec2 topLeft,
    float maxW,
    const std::vector<std::pair<std::string,std::string>>& stats,
    const PreviewTextGradient& keyGradient,
    ImU32 valCol,   // colour for value text (always white)
    float fontSize)
{
    float y = topLeft.y;
    ImFont* font = ImGui::GetFont();
    float lineH  = fontSize + 2.f;
    int   n      = (int)stats.size();
    for(int i=0;i<n;++i){
        bool hasKey = !stats[i].first.empty();
        std::string keyStr = hasKey ? (stats[i].first + ":") : std::string();
        std::string valStr;
        if(hasKey){
            valStr = stats[i].second.empty() ? std::string() : (" " + stats[i].second);
        } else {
            valStr = stats[i].second;
        }
        float x = topLeft.x;
        if(hasKey){
            int keyChars = (int)keyStr.size();
            for(int j=0;j<keyChars;++j){
                float t = (keyChars>1) ? (float)j/(keyChars-1) : 0.f;
                ImU32 kc = SamplePreviewTextGradient(keyGradient, t);
                char ch[2] = { keyStr[j], '\0' };
                dl->AddText(font, fontSize, {x, y}, kc, ch);
                x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, ch).x;
            }
        }
        dl->AddText(font, fontSize, {x, y}, valCol, valStr.c_str());
        y += lineH;
        if(y + lineH > topLeft.y + 210.f) break;
    }
    return y;
}

enum class TextFileEncoding {
    Utf8,
    Utf8Bom,
    Utf16LeBom
};

struct TextFileData {
    std::wstring text;
    TextFileEncoding encoding = TextFileEncoding::Utf8;
};

static std::wstring Utf8ToWide(const std::string& text){
    if(text.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
    if(needed <= 0) return {};
    std::wstring out((size_t)needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), needed);
    return out;
}

static std::string WideToUtf8(const std::wstring& text){
    if(text.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    if(needed <= 0) return {};
    std::string out((size_t)needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), needed, nullptr, nullptr);
    return out;
}

static bool ReadTextFilePreserveEncoding(const std::string& path, TextFileData& out){
    std::ifstream in(path, std::ios::binary);
    if(!in) return false;

    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if(bytes.size() >= 2 &&
       (unsigned char)bytes[0] == 0xFF &&
       (unsigned char)bytes[1] == 0xFE){
        out.encoding = TextFileEncoding::Utf16LeBom;
        const size_t wcharCount = (bytes.size() - 2) / sizeof(wchar_t);
        out.text.assign(reinterpret_cast<const wchar_t*>(bytes.data() + 2), wcharCount);
        return true;
    }

    if(bytes.size() >= 3 &&
       (unsigned char)bytes[0] == 0xEF &&
       (unsigned char)bytes[1] == 0xBB &&
       (unsigned char)bytes[2] == 0xBF){
        out.encoding = TextFileEncoding::Utf8Bom;
        out.text = Utf8ToWide(bytes.substr(3));
        return true;
    }

    out.encoding = TextFileEncoding::Utf8;
    out.text = Utf8ToWide(bytes);
    return true;
}

static bool WriteTextFilePreserveEncoding(const std::string& path, const TextFileData& data){
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if(!out) return false;

    switch(data.encoding){
        case TextFileEncoding::Utf16LeBom: {
            const unsigned char bom[2] = {0xFF, 0xFE};
            out.write(reinterpret_cast<const char*>(bom), sizeof(bom));
            if(!data.text.empty())
                out.write(reinterpret_cast<const char*>(data.text.data()), (std::streamsize)(data.text.size() * sizeof(wchar_t)));
            return (bool)out;
        }
        case TextFileEncoding::Utf8Bom: {
            const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
            out.write(reinterpret_cast<const char*>(bom), sizeof(bom));
            const std::string utf8 = WideToUtf8(data.text);
            if(!utf8.empty()) out.write(utf8.data(), (std::streamsize)utf8.size());
            return (bool)out;
        }
        case TextFileEncoding::Utf8:
        default: {
            const std::string utf8 = WideToUtf8(data.text);
            if(!utf8.empty()) out.write(utf8.data(), (std::streamsize)utf8.size());
            return (bool)out;
        }
    }
}

// ── Preview card (image + stat overlay) ──────────────────────────────────
struct PreviewCard {
    std::string name;
    std::string imgPath;
    int resourceId = 0;
    ID3D11ShaderResourceView* srv = nullptr;
    int imgW=0, imgH=0;

    // Returns true when image is ready. Retries until success or g_dev absent.
    bool tryLoad(){
        if(srv) return true;          // already loaded
        if(!g_dev) return false;
        if(resourceId != 0 && apb::LoadTextureFromResource(g_dev,resourceId,&srv,&imgW,&imgH))
            return true;
        if(imgPath.empty()) return false;
        if(apb::LoadTextureFromFile(g_dev,imgPath,&srv,&imgW,&imgH))
            return true;
        // Fallback: search common runtime roots (exe dir, build parents, cwd, app-doc assets)
        namespace fs = std::filesystem;
        std::string fn=imgPath; auto slash=fn.find_last_of("/\\");
        if(slash!=std::string::npos) fn=fn.substr(slash+1);

        std::vector<std::string> subDirs;
        if(imgPath.find("Vehicle")!=std::string::npos){
            subDirs = {"Images_VehicleItemTypes"};
        } else {
            subDirs = {"Images_WeaponItemTypes"};
        }

        std::vector<fs::path> roots;
        char exeBuf[MAX_PATH]={};
        GetModuleFileNameA(nullptr,exeBuf,MAX_PATH);
        fs::path exeDir = fs::path(exeBuf).parent_path();
        for(int i=0;i<4 && !exeDir.empty();++i){
            roots.push_back(exeDir);
            exeDir = exeDir.parent_path();
        }
        fs::path cwd = fs::current_path();
        for(int i=0;i<3 && !cwd.empty();++i){
            roots.push_back(cwd);
            cwd = cwd.parent_path();
        }
        roots.push_back(fs::path(apb::AppDocDir()));
        roots.push_back(fs::path(apb::AssetsDir()));

        std::vector<std::string> candidates;
        for(const auto& root : roots){
            for(const auto& sub : subDirs){
                candidates.push_back((root/"Assets"/sub/fn).string());
                candidates.push_back((root/sub/fn).string());
            }
        }

        for(const auto& alt : candidates){
            if(apb::LoadTextureFromFile(g_dev,alt,&srv,&imgW,&imgH)) break;
        }
        return srv != nullptr;
    }
    void release(){ apb::ReleaseTexture(srv); }

    // Draw at pos, returns bottom y
    // keyStart→keyEnd are gradient endpoints for stat key text
    float draw(ImDrawList* dl, ImVec2 pos, float displayW,
               const std::vector<std::pair<std::string,std::string>>& stats,
               const PreviewTextGradient& keyGradient, ImU32 valCol,
               float overlayYOffset = 0.0f,
               float overlayXOffset = 0.0f) const
    {
        float scale = displayW / (imgW > 0 ? (float)imgW : 327.f);
        float dh    = (imgH > 0 ? imgH : 362.f) * scale;

        if(srv){
            dl->AddImage((ImTextureID)srv, pos, {pos.x+displayW, pos.y+dh});
        } else {
            // No image: plain dark card with name
            dl->AddRectFilled(pos,{pos.x+displayW,pos.y+dh},IM_COL32(25,25,25,255));
            dl->AddRect(pos,{pos.x+displayW,pos.y+dh},IM_COL32(60,60,60,255));
        }

        // Overlay — Python OVERLAY_RECT_PCT: left=4.5%, top=44%
        float ol = displayW * 0.045f;
        float ot = dh       * 0.440f;
        float ow = displayW * (1.f - 0.045f - 0.030f);
        ImVec2 overlayPos = {pos.x+ol + overlayXOffset, pos.y+ot + overlayYOffset};

        float fs = ImGui::GetFontSize();
        DrawStatLines(dl, overlayPos, ow, stats, keyGradient, valCol, fs);

        return pos.y + dh;
    }
};

// ── Colour scheme section used by both Weapon and Vehicle pages ───────────
struct ColourSchemeWidget {
    int   schemeIdx = 0;  // 0=Vanilla,1=Stats,2=Single,3=Gradient,4=Triple
    bool  presetEquipped = false;
    int   presetThemeIdx = -1;
    float singleCol[3] = {1,1,1};
    float gradStart[3] = {1.f,1.f,1.f};
    float tripleMid[3] = {1.f,1.f,1.f};
    float gradEnd[3]   = {1.f,1.f,1.f};

    // Base scheme labels (indices 0-4 are fixed)
    static constexpr const char* BASE_SCHEMES[] = {
        "Vanilla", "APB.DB Stats", "APB.DB Stats - single colour",
        "APB.DB Stats - gradient", "APB.DB Stats - Triple Gradient"
    };
    static constexpr int BASE_COUNT = 5;

    void resetDefaults(){
        schemeIdx=0;
        presetEquipped=false;
        presetThemeIdx=-1;
        singleCol[0]=1.f; singleCol[1]=1.f; singleCol[2]=1.f;
        setManualGradientWhite();
    }

    void setManualGradientWhite(){
        gradStart[0]=1.f; gradStart[1]=1.f; gradStart[2]=1.f;
        tripleMid[0]=1.f; tripleMid[1]=1.f; tripleMid[2]=1.f;
        gradEnd[0]=1.f;   gradEnd[1]=1.f;   gradEnd[2]=1.f;
    }

    const char* currentStyleLabel(const ThemeLibrary& lib) const {
        if(presetEquipped && presetThemeIdx>=0 && presetThemeIdx<lib.count()) return "Preset";
        return BASE_SCHEMES[schemeIdx];
    }

    void applyPresetTheme(int idx){
        auto& lib = ThemeLib();
        if(idx<0 || idx>=lib.count()) return;
        presetEquipped = true;
        presetThemeIdx = idx;
        schemeIdx = 3; // Presets are gradient-backed
        gradStart[0]=(float)lib.themes[idx].smoothStart.r;
        gradStart[1]=(float)lib.themes[idx].smoothStart.g;
        gradStart[2]=(float)lib.themes[idx].smoothStart.b;
        tripleMid[0]=(float)lib.themes[idx].tripleMiddle.r;
        tripleMid[1]=(float)lib.themes[idx].tripleMiddle.g;
        tripleMid[2]=(float)lib.themes[idx].tripleMiddle.b;
        gradEnd[0]=(float)lib.themes[idx].smoothEnd.r;
        gradEnd[1]=(float)lib.themes[idx].smoothEnd.g;
        gradEnd[2]=(float)lib.themes[idx].smoothEnd.b;
    }

    // Gradient colours currently in use
    void resolvedGrad(RGB& gs, RGB& ge) const {
        if(presetEquipped){
            auto& lib = ThemeLib();
            if(presetThemeIdx>=0 && presetThemeIdx<lib.count()){
                gs = lib.themes[presetThemeIdx].smoothStart;
                ge = lib.themes[presetThemeIdx].smoothEnd;
                return;
            }
        }
        gs = {gradStart[0],gradStart[1],gradStart[2]};
        ge = {gradEnd[0],  gradEnd[1],  gradEnd[2]};
    }

    void resolvedTriple(RGB& gs, RGB& gm, RGB& ge) const {
        if(presetEquipped){
            auto& lib = ThemeLib();
            if(presetThemeIdx>=0 && presetThemeIdx<lib.count()){
                gs = lib.themes[presetThemeIdx].smoothStart;
                gm = lib.themes[presetThemeIdx].tripleMiddle;
                ge = lib.themes[presetThemeIdx].smoothEnd;
                return;
            }
        }
        gs = {gradStart[0], gradStart[1], gradStart[2]};
        gm = {tripleMid[0], tripleMid[1], tripleMid[2]};
        ge = {gradEnd[0], gradEnd[1], gradEnd[2]};
    }

    void draw(const char* uid, bool& changed){
        changed = false;
        auto& lib = ThemeLib();
        if(schemeIdx < 0 || schemeIdx >= BASE_COUNT) schemeIdx = 0;
        if(presetEquipped && (presetThemeIdx<0 || presetThemeIdx>=lib.count())){
            presetEquipped = false;
            presetThemeIdx = -1;
        }

        SectionNote("Choose a manual style or load a preset theme for the stat overlay.");

        char tid[32]; snprintf(tid, sizeof(tid), "##cst%s", uid);
        if(BeginSectionTable(tid, 116.f, 184.f)){
            BeginSectionRow("Style");
            ImGui::SetNextItemWidth(-FLT_MIN);
            char cid[32]; snprintf(cid,sizeof(cid),"##sc%s",uid);
            if(ImGui::BeginCombo(cid, currentStyleLabel(lib))){
                for(int i=0;i<BASE_COUNT;++i){
                    if(ImGui::Selectable(BASE_SCHEMES[i], schemeIdx==i)){
                        const bool leavingPresetForManualGradient = presetEquipped && (i == 3 || i == 4);
                        schemeIdx=i;
                        presetEquipped=false;
                        presetThemeIdx=-1;
                        if(leavingPresetForManualGradient)
                            setManualGradientWhite();
                        changed=true;
                    }
                }
                ImGui::EndCombo();
            }

            BeginSectionRow("Preset");
            ImGui::SetNextItemWidth(-FLT_MIN);
            char pid[32]; snprintf(pid,sizeof(pid),"##pr%s",uid);
            std::string previewName = (presetEquipped && presetThemeIdx>=0 && presetThemeIdx<lib.count())
                ? lib.themes[presetThemeIdx].name
                : "-- select --";
            if(ImGui::BeginCombo(pid, previewName.c_str())){
                for(int i=0;i<lib.count();++i){
                    bool sel = (presetEquipped && presetThemeIdx==i);
                    if(ImGui::Selectable(lib.themes[i].name.c_str(), sel)){
                        applyPresetTheme(i);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            NextSectionAction();
            char rbid[32]; snprintf(rbid,sizeof(rbid),"Reload##pr%s",uid);
            if(ImGui::SmallButton(rbid)){
                lib.reload();
                if(presetEquipped && (presetThemeIdx<0 || presetThemeIdx>=lib.count())){
                    presetEquipped=false;
                    presetThemeIdx=-1;
                    changed=true;
                }
            }
            ImGui::SameLine();
            char opfbid[40]; snprintf(opfbid,sizeof(opfbid),"Open Folder##pr%s",uid);
            if(ImGui::SmallButton(opfbid)) OpenInExplorer(ThemeLibrary::themesDir());

            if(!presetEquipped && schemeIdx==2){
                BeginSectionRow("Single Colour");
                char singleId[32]; snprintf(singleId,sizeof(singleId),"##sing%s",uid);
                if(ColorPickerButton(singleId, singleCol)) changed=true;
            }
            if(!presetEquipped && schemeIdx==3){
                BeginSectionRow("Gradient");
                char p1[32],p2[32];
                snprintf(p1,sizeof(p1),"##gs%s",uid);
                snprintf(p2,sizeof(p2),"##ge%s",uid);
                if(ColorPickerButton(p1,gradStart)) changed=true;
                ImGui::SameLine();
                ImGui::TextUnformatted("Start");
                ImGui::SameLine();
                if(ColorPickerButton(p2,gradEnd)) changed=true;
                ImGui::SameLine();
                ImGui::TextUnformatted("End");
            }
            if(!presetEquipped && schemeIdx==4){
                BeginSectionRow("Triple");
                char p1[32],pm[32],p2[32];
                snprintf(p1,sizeof(p1),"##gts%s",uid);
                snprintf(pm,sizeof(pm),"##gtm%s",uid);
                snprintf(p2,sizeof(p2),"##gte%s",uid);
                if(ColorPickerButton(p1,gradStart)) changed=true;
                ImGui::SameLine();
                ImGui::TextUnformatted("1");
                ImGui::SameLine();
                if(ColorPickerButton(pm,tripleMid)) changed=true;
                ImGui::SameLine();
                ImGui::TextUnformatted("2");
                ImGui::SameLine();
                if(ColorPickerButton(p2,gradEnd)) changed=true;
                ImGui::SameLine();
                ImGui::TextUnformatted("3");
            }

            BeginSectionRow("Actions");
            char defbid[32]; snprintf(defbid,sizeof(defbid),"Defaults##df%s",uid);
            if(ImGui::Button(defbid)){ resetDefaults(); changed=true; }
            EndSectionTable();
        }
    }

    // Returns the Scheme enum and RGBs for use with the backend
    void getScheme(Scheme& sc, RGB& single, RGB& gs, RGB& ge) const {
        if(presetEquipped){
            sc=Scheme::GRADIENT;
        } else if(schemeIdx==0) sc=Scheme::CLEAR;
        else if(schemeIdx==1) sc=Scheme::CLEAR;
        else if(schemeIdx==2) sc=Scheme::SINGLE;
        else if(schemeIdx==4) sc=Scheme::TRIPLE;
        else sc=Scheme::GRADIENT;
        single = (sc==Scheme::TRIPLE)
            ? RGB{tripleMid[0],tripleMid[1],tripleMid[2]}
            : RGB{singleCol[0],singleCol[1],singleCol[2]};
        resolvedGrad(gs,ge);
    }
};

// ══════════════════════════════════════════════════════════════════════════
// PageInventoryItemTypes
// ══════════════════════════════════════════════════════════════════════════
struct PageInventoryItemTypes {
    char gerPath[MAX_PATH]={}, intPath[MAX_PATH]={}, outPath[MAX_PATH]={};
    ThreadLog log;
    std::atomic<bool> running{false};
    std::string lastOut;

    void draw(){
        ImGui::BeginChild("##iit",{0,0},false);
        SectionLabel("InventoryItemTypes – Merge GER \xe2\x86\x90 INT");
        SectionNote("Merge translated entries into a target file and review the generated report below.");

        auto row=[&](const char* label,char* buf,const char* id,const char* filter,bool isSave=false){
            BeginSectionRow(label);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText(id,buf,MAX_PATH);
            NextSectionAction();
            char bid[32]; snprintf(bid,sizeof(bid),"Browse##%s",id);
            if(ImGui::Button(bid)){
                std::string s;
                if(isSave) BrowseSaveFile(s,filter);
                else       BrowseFile(s,filter);
                if(!s.empty()) strncpy(buf,s.c_str(),MAX_PATH-1);
            }
        };

        SectionLabel("Files");
        if(BeginSectionTable("##iitfiles", 116.f, 86.f)){
            row("GER File", gerPath, "##iitger", "All Files\0*.*\0");
            row("INT File", intPath, "##iitint", "All Files\0*.*\0");
            row("Output", outPath, "##iitout", "Text Files\0*.txt\0", true);
            EndSectionTable();
        }

        SectionLabel("Run");
        bool busy=running.load();
        if(busy) ImGui::BeginDisabled();
        if(RunButton("Run##iit")){
            if(gerPath[0]&&intPath[0]&&!running.load()){
                log.clear(); running=true; lastOut.clear();
                std::string g=gerPath,i=intPath,o=outPath;
                std::thread([this,g,i,o](){
                    try{
                        auto r=mergeInventoryItemTypes(g,i,o.empty()?std::string{}:o);
                        lastOut=r.outputPath;
                        log.append("Done \xe2\x86\x92 "+r.outputPath);
                        if(!r.reportText.empty()) log.append(r.reportText);
                    }catch(std::exception& e){log.append(std::string("Error: ")+e.what());}
                    running=false;
                }).detach();
            }
        }
        if(busy) ImGui::EndDisabled();
        ImGui::SameLine();
        if(!lastOut.empty()&&ImGui::Button("Open Output",{110,32})) OpenInExplorer(lastOut);
        if(busy){ ImGui::SameLine(); ImGui::TextColored(Col::YELLOW,"Running..."); }

        SectionLabel("Log");
        std::string t=log.get();
        ReadOnlyLogBox("##iitlog", t, {-1.f, -1.f});
        ImGui::EndChild();
    }
};

// ══════════════════════════════════════════════════════════════════════════
// PagePlayerRoles
// ══════════════════════════════════════════════════════════════════════════
struct PagePlayerRoles {
    char intPath[MAX_PATH] = {}, outPath[MAX_PATH] = {};
    int nameStyleIdx = 1;
    bool useShortEquipmentNames = false;
    float solidCol[3] = {1.f, 1.f, 1.f};
    float gradStart[3] = {0.08f, 0.00f, 0.78f};
    float gradMiddle[3] = {0.36f, 0.08f, 0.82f};
    float gradEnd[3] = {0.65f, 0.02f, 0.40f};
    Progress prog;
    ThreadLog log;
    std::atomic<bool> running{false};
    std::atomic<bool> cancelRequested{false};
    std::string lastOut;
    bool autoDetectTried = false;
    std::vector<PreviewCard> previewCards;
    bool previewCardsInit = false;
    bool previewCardsShortNames = false;

    void configurePreviewCard(PreviewCard& card, const std::string& imgPath, int resourceId){
        const bool imageChanged = card.imgPath != imgPath || card.resourceId != resourceId;
        if(imageChanged){
            card.release();
            card.imgPath = imgPath;
            card.resourceId = resourceId;
            card.imgW = 0;
            card.imgH = 0;
        }
        card.tryLoad();
    }

    void initPreviewCards(){
        if(!previewCardsInit){
            previewCardsInit = true;
            previewCards.resize(4);
        }
        if(previewCardsShortNames == useShortEquipmentNames && previewCards[0].srv)
            return;

        previewCardsShortNames = useShortEquipmentNames;
        configurePreviewCard(previewCards[0], "Assets\\Images_PlayerRoles\\gunslinger.png",
            IDR_IMG_PLAYER_ROLE_GUNSLINGER);
        configurePreviewCard(previewCards[1], "Assets\\Images_PlayerRoles\\rifleman.png",
            IDR_IMG_PLAYER_ROLE_RIFLEMAN);
        configurePreviewCard(previewCards[2],
            useShortEquipmentNames
                ? "Assets\\Images_PlayerRoles\\burglar_short.png"
                : "Assets\\Images_PlayerRoles\\burglar.png",
            useShortEquipmentNames
                ? IDR_IMG_PLAYER_ROLE_BURGLAR_SHORT
                : IDR_IMG_PLAYER_ROLE_BURGLAR);
        configurePreviewCard(previewCards[3], "Assets\\Images_PlayerRoles\\epidemic_2024.png",
            IDR_IMG_PLAYER_ROLE_EPIDEMIC2024);
    }

    static std::string latestLogLine(const std::string& text){
        const size_t end = text.find_last_not_of("\r\n\t ");
        if(end == std::string::npos) return {};
        size_t start = text.find_last_of('\n', end);
        start = (start == std::string::npos) ? 0 : start + 1;
        std::string line = text.substr(start, end - start + 1);
        while(!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        return line;
    }

    void drawPreviewGallery(){
        SectionLabel("Preview");

        const std::string status = latestLogLine(log.get());
        if(!status.empty()){
            ImGui::TextColored(Col::SUBTEXT, "%s", status.c_str());
            ImGui::Spacing();
        }

        const float availW = ImGui::GetContentRegionAvail().x;
        const float gap = 12.f;
        const float preferredImageW = 245.f;
        const float minimumImageW = 170.f;
        int columns = 1;
        for(int candidate = 4; candidate >= 1; --candidate){
            const float candidateW = (availW - gap * (candidate - 1)) / candidate;
            if(candidateW >= minimumImageW){
                columns = candidate;
                break;
            }
        }
        const float imageW = std::min(preferredImageW,
            (availW - gap * (columns - 1)) / columns);

        ImGui::BeginChild("##prolespreview", {0, 0}, false);
        for(int i = 0; i < (int)previewCards.size(); ++i){
            auto& card = previewCards[i];
            card.tryLoad();

            if(columns > 1 && (i % columns) != 0)
                ImGui::SameLine(0.f, gap);
            else if(i > 0)
                ImGui::Spacing();

            if(card.srv){
                const float scale = imageW / (card.imgW > 0 ? (float)card.imgW : imageW);
                const float imageH = (card.imgH > 0 ? (float)card.imgH : imageW) * scale;
                ImGui::Image((ImTextureID)card.srv, {imageW, imageH});
                ImGui::GetWindowDrawList()->AddRect(
                    ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                    ImGui::GetColorU32(Col::BORDER));
            } else {
                ImGui::BeginChild(
                    ("##prolesmissing" + std::to_string(i)).c_str(),
                    {imageW, imageW * 1.8f}, true);
                ImGui::TextColored(Col::SUBTEXT, "Preview unavailable");
                ImGui::EndChild();
            }
        }
        ImGui::EndChild();
    }

    void ensureDefaultIntPath(){
        if(autoDetectTried || intPath[0]) return;
        autoDetectTried = true;
        const std::string detected = DetectApbIntFile("PlayerRoles.INT");
        if(!detected.empty()) strncpy(intPath, detected.c_str(), MAX_PATH - 1);
    }

    bool isActionRunning() const { return running.load(); }
    bool canStartAction() const { return !running.load() && intPath[0]; }

    PlayerRoleNameStyle selectedNameStyle() const {
        switch(nameStyleIdx){
            case 0: return PlayerRoleNameStyle::NONE;
            case 2: return PlayerRoleNameStyle::STEPPED;
            case 3: return PlayerRoleNameStyle::SMOOTH;
            case 4: return PlayerRoleNameStyle::TRIPLE;
            case 1:
            default: return PlayerRoleNameStyle::SOLID;
        }
    }

    void startAction(){
        if(!canStartAction()) return;
        log.clear();
        lastOut.clear();
        cancelRequested = false;
        running = true;
        prog.reset(0);

        const std::string ip = intPath;
        const std::string op = outPath;
        const PlayerRoleNameStyle style = selectedNameStyle();
        const RGB solid{solidCol[0], solidCol[1], solidCol[2]};
        const RGB gs{gradStart[0], gradStart[1], gradStart[2]};
        const RGB gm{gradMiddle[0], gradMiddle[1], gradMiddle[2]};
        const RGB ge{gradEnd[0], gradEnd[1], gradEnd[2]};

        const bool shortEquipmentNames = useShortEquipmentNames;

        std::thread([this, ip, op, style, shortEquipmentNames, solid, gs, gm, ge](){
            try{
                PlayerRolesResult res = generatePlayerRolesFile(
                    ip, op, style, shortEquipmentNames, solid, gs, gm, ge,
                    [this](int done, int total, const std::string& label){
                        prog.set(done, total, label);
                    },
                    &cancelRequested);

                if(res.cancelled || cancelRequested.load()){
                    log.append("Cancelled.");
                } else {
                    lastOut = res.outputPath;
                    log.append("Updated " + std::to_string(res.updatedKeys.size()) + " roles.");
                    if(!res.failedKeys.empty()){
                        log.append("Kept existing file values for " + std::to_string(res.failedKeys.size()) + " roles:");
                        for(const auto& key : res.failedKeys) log.append("  " + key);
                    }
                    log.append("Updated file -> " + res.outputPath);
                }
            } catch(const std::exception& e){
                log.append(std::string("Error: ") + e.what());
            }

            cancelRequested = false;
            running = false;
        }).detach();
    }

    void cancelAction(){
        if(!running.load()) return;
        cancelRequested = true;
        log.append("Cancelling...");
    }

    void draw(){
        initPreviewCards();
        ensureDefaultIntPath();
        ImGui::BeginChild("##proles", {0, 0}, false);
        SectionLabel("Player Roles – generate organized PlayerRoles output from APB.DB");
        SectionNote("Build a fresh PlayerRoles output file with consistent colour formatting and optional short equipment names.");

        SectionLabel("Files");
        if(BeginSectionTable("##prolesfiles", 124.f, 86.f)){
            BeginSectionRow("INT File");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##prolesint", intPath, MAX_PATH);
            NextSectionAction();
            if(ImGui::Button("Browse##prolesint")){
                std::string s;
                if(BrowseFile(s, "INT Files\0*.int;*.INT\0\0")) strncpy(intPath, s.c_str(), MAX_PATH - 1);
            }

            BeginSectionRow("Output Location");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##prolesout", outPath, MAX_PATH);
            NextSectionAction();
            if(ImGui::Button("Browse##prolesout")){
                std::string s;
                if(BrowseSaveFile(s, "INT Files\0*.int\0GER Files\0*.ger\0Text Files\0*.txt\0\0", "int"))
                    strncpy(outPath, s.c_str(), MAX_PATH - 1);
            }
            EndSectionTable();
        }
        SectionNote("Leave Output Location blank to create a new file in Downloads.");

        static constexpr const char* STYLE_LABELS[] = {
            "No Colour", "Solid", "Stepped", "Smooth", "Triple Gradient"
        };
        SectionLabel("Formatting");
        if(BeginSectionTable("##prolesformat", 124.f, 86.f)){
            BeginSectionRow("Role Name Style");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if(ImGui::BeginCombo("##prolesstyle", STYLE_LABELS[(nameStyleIdx >= 0 && nameStyleIdx < 5) ? nameStyleIdx : 1])){
                for(int i = 0; i < 5; ++i){
                    const bool selected = (nameStyleIdx == i);
                    if(ImGui::Selectable(STYLE_LABELS[i], selected)) nameStyleIdx = i;
                    if(selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if(nameStyleIdx == 1){
                BeginSectionRow("Colour");
                ColorPickerButton("##prolessolid", solidCol);
            } else if(nameStyleIdx == 2 || nameStyleIdx == 3){
                BeginSectionRow("Gradient");
                ColorPickerButton("##prolesstart", gradStart);
                ImGui::SameLine();
                ImGui::TextUnformatted("Start");
                ImGui::SameLine();
                ColorPickerButton("##prolesend", gradEnd);
                ImGui::SameLine();
                ImGui::TextUnformatted("End");
            } else if(nameStyleIdx == 4){
                BeginSectionRow("Triple Gradient");
                ColorPickerButton("##prolestri_start", gradStart);
                ImGui::SameLine();
                ImGui::TextUnformatted("1");
                ImGui::SameLine();
                ColorPickerButton("##prolestri_mid", gradMiddle);
                ImGui::SameLine();
                ImGui::TextUnformatted("2");
                ImGui::SameLine();
                ColorPickerButton("##prolestri_end", gradEnd);
                ImGui::SameLine();
                ImGui::TextUnformatted("3");
            }

            BeginSectionRow("Equipment Names");
            ImGui::Checkbox("Use short equipment names", &useShortEquipmentNames);
            EndSectionTable();
        }

        SectionLabel("Progress");
        ImGui::ProgressBar(prog.frac(), {-1, 18});
        ImGui::TextColored(Col::SUBTEXT, "%d / %d  %s",
            prog.done.load(), prog.total.load(), prog.lbl().c_str());

        const bool busy = running.load();
        if(!busy){
            if(RunButton("Generate##proles")) startAction();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, Col::RED);
            if(ImGui::Button("Stop", {120, 32})) cancelAction();
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextColored(Col::YELLOW, "Fetching roles...");
        }

        ImGui::SameLine();
        if(!lastOut.empty() && ImGui::Button("Open Output", {110, 32}))
            OpenInExplorer(lastOut);

        drawPreviewGallery();
        ImGui::EndChild();
    }
};

// ══════════════════════════════════════════════════════════════════════════
// PageContactDescription
// ══════════════════════════════════════════════════════════════════════════
struct PageContactDescription {
    char intPath[MAX_PATH] = {}, outPath[MAX_PATH] = {};
    int modeIdx = 0;
    Progress prog;
    ThreadLog log;
    std::atomic<bool> running{false};
    std::atomic<bool> cancelRequested{false};
    std::string lastOut;
    bool autoDetectTried = false;

    void ensureDefaultIntPath(){
        if(autoDetectTried || intPath[0]) return;
        autoDetectTried = true;
        const std::string detected = DetectApbIntFile("Contacts.INT");
        if(!detected.empty()) strncpy(intPath, detected.c_str(), MAX_PATH - 1);
    }

    bool isActionRunning() const { return running.load(); }
    bool canStartAction() const { return !running.load() && intPath[0]; }

    ContactDescriptionMode selectedMode() const {
        return modeIdx == 1 ? ContactDescriptionMode::MISSIONS : ContactDescriptionMode::UNLOCKS;
    }

    void startAction(){
        if(!canStartAction()) return;
        log.clear();
        lastOut.clear();
        cancelRequested = false;
        running = true;
        prog.reset(0);

        const std::string ip = intPath;
        const std::string op = outPath;
        const ContactDescriptionMode mode = selectedMode();

        std::thread([this, ip, op, mode](){
            try{
                ContactDescriptionsResult res = generateContactDescriptionsFile(
                    ip, op, mode,
                    [this](int done, int total, const std::string& label){
                        prog.set(done, total, label);
                    },
                    &cancelRequested);

                if(res.cancelled || cancelRequested.load()){
                    log.append("Cancelled.");
                } else {
                    lastOut = res.outputPath;
                    log.append("Updated " + std::to_string(res.updatedKeys.size()) + " contacts.");
                    if(!res.failedKeys.empty()){
                        log.append("Skipped " + std::to_string(res.failedKeys.size()) + " contacts:");
                        for(const auto& key : res.failedKeys) log.append("  " + key);
                    }
                    log.append("Updated file -> " + res.outputPath);
                }
            } catch(const std::exception& e){
                log.append(std::string("Error: ") + e.what());
            }

            cancelRequested = false;
            running = false;
        }).detach();
    }

    void cancelAction(){
        if(!running.load()) return;
        cancelRequested = true;
        log.append("Cancelling...");
    }

    void draw(){
        ensureDefaultIntPath();
        ImGui::BeginChild("##contactsdesc", {0, 0}, false);
        SectionLabel("Contact Description – generate Contacts descriptions from APB.DB");
        SectionNote("Generate contact unlock or mission descriptions directly from APB.DB-backed source files.");

        SectionLabel("Files");
        if(BeginSectionTable("##contactsfiles", 124.f, 86.f)){
            BeginSectionRow("Source File");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##contactsint", intPath, MAX_PATH);
            NextSectionAction();
            if(ImGui::Button("Browse##contactsint")){
                std::string s;
                if(BrowseFile(s, "Localization Files\0*.int;*.INT;*.ger;*.GER\0\0"))
                    strncpy(intPath, s.c_str(), MAX_PATH - 1);
            }

            BeginSectionRow("Output Location");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##contactsout", outPath, MAX_PATH);
            NextSectionAction();
            if(ImGui::Button("Browse##contactsout")){
                std::string s;
                if(BrowseSaveFile(s, "INT Files\0*.int\0GER Files\0*.ger\0Text Files\0*.txt\0\0", "int"))
                    strncpy(outPath, s.c_str(), MAX_PATH - 1);
            }
            EndSectionTable();
        }
        SectionNote("Leave Output Location blank to create a new file in Downloads.");

        static constexpr const char* MODE_LABELS[] = {"UNLOCKS", "MISSIONS"};
        SectionLabel("Options");
        if(BeginSectionTable("##contactsmodegrid", 124.f, 86.f)){
            BeginSectionRow("Mode");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if(ImGui::BeginCombo("##contactsmode", MODE_LABELS[(modeIdx >= 0 && modeIdx < 2) ? modeIdx : 0])){
                for(int i = 0; i < 2; ++i){
                    const bool selected = (modeIdx == i);
                    if(ImGui::Selectable(MODE_LABELS[i], selected)) modeIdx = i;
                    if(selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            EndSectionTable();
        }

        SectionLabel("Progress");
        ImGui::ProgressBar(prog.frac(), {-1, 18});
        ImGui::TextColored(Col::SUBTEXT, "%d / %d  %s",
            prog.done.load(), prog.total.load(), prog.lbl().c_str());

        const bool busy = running.load();
        if(!busy){
            if(RunButton("Generate##contacts")) startAction();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, Col::RED);
            if(ImGui::Button("Stop", {120, 32})) cancelAction();
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextColored(Col::YELLOW, "Fetching contacts...");
        }

        ImGui::SameLine();
        if(!lastOut.empty() && ImGui::Button("Open Output", {110, 32}))
            OpenInExplorer(lastOut);

        SectionLabel("Log");
        std::string t = log.get();
        ReadOnlyLogBox("##contactslog", t, {-1.f, -1.f});
        ImGui::EndChild();
    }
};

// ══════════════════════════════════════════════════════════════════════════
// PageReloadResupplyText
// ══════════════════════════════════════════════════════════════════════════
struct PageReloadResupplyText {
    struct TextStyleState {
        char text[512] = {};
        int fontIdx = 0;
        int modeIdx = 0; // 0=SOLID, 1=Stepped, 2=Smooth, 3=Triple Gradient
        float solid[3] = {1.f, 1.f, 1.f};
        float stepped[6][3] = {
            {1.f,1.f,1.f}, {1.f,1.f,1.f}, {1.f,1.f,1.f},
            {1.f,1.f,1.f}, {1.f,1.f,1.f}, {1.f,1.f,1.f}
        };
        float smoothStart[3] = {1.f, 1.f, 1.f};
        float smoothEnd[3] = {1.f, 1.f, 1.f};
        float tripleStart[3] = {1.f, 1.f, 1.f};
        float tripleMid[3] = {1.f, 1.f, 1.f};
        float tripleEnd[3] = {1.f, 1.f, 1.f};
    };

    char gerPath[MAX_PATH] = {};
    int activeTabIdx = 0;
    bool applySavedTabSelection = true;
    TextStyleState reload;
    TextStyleState resupply;
    std::vector<std::string> fonts;
    ThreadLog log;
    std::string lastSavedPath;
    bool autoDetectTried = false;
    bool autoLoadAttempted = false;
    TextFileEncoding fileEncoding = TextFileEncoding::Utf16LeBom;
    ID3D11ShaderResourceView* backgroundSrv = nullptr;
    int backgroundW = 0;
    int backgroundH = 0;

    static constexpr const char* MODES[] = {"SOLID", "Stepped", "Smooth", "Triple Gradient"};

    PageReloadResupplyText(){
        fonts = availableFonts();
        resetStyle(reload, "Reload");
        resetStyle(resupply, "Resupply");
    }

    static RGB rgbFromFloats(const float c[3]){
        return {c[0], c[1], c[2]};
    }

    static void copyColor(float dst[3], const float src[3]){
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
    }

    static bool sameRgb(const RGB& a, const RGB& b, float epsilon = 0.0015f){
        return std::fabs((float)(a.r - b.r)) <= epsilon &&
               std::fabs((float)(a.g - b.g)) <= epsilon &&
               std::fabs((float)(a.b - b.b)) <= epsilon;
    }

    static std::vector<int> bouncePattern(int m){
        if(m <= 1) return {0};
        std::vector<int> out;
        for(int i = 0; i < m; ++i) out.push_back(i);
        for(int i = m - 2; i >= 1; --i) out.push_back(i);
        return out;
    }

    static void resetStyle(TextStyleState& state, const char* defaultText){
        std::snprintf(state.text, sizeof(state.text), "%s", defaultText ? defaultText : "");
        state.fontIdx = 0;
        state.modeIdx = 0;
        state.solid[0] = state.solid[1] = state.solid[2] = 1.f;
        state.smoothStart[0] = state.smoothStart[1] = state.smoothStart[2] = 1.f;
        state.smoothEnd[0] = state.smoothEnd[1] = state.smoothEnd[2] = 1.f;
        state.tripleStart[0] = state.tripleStart[1] = state.tripleStart[2] = 1.f;
        state.tripleMid[0] = state.tripleMid[1] = state.tripleMid[2] = 1.f;
        state.tripleEnd[0] = state.tripleEnd[1] = state.tripleEnd[2] = 1.f;
        for(auto& step : state.stepped){
            step[0] = step[1] = step[2] = 1.f;
        }
    }

    void ensureDefaultGerPath(){
        if(autoDetectTried || gerPath[0]) return;
        autoDetectTried = true;
        const std::string detected = DetectApbLocalizationFile("GER", "APBUserInterface.GER");
        if(!detected.empty()) std::snprintf(gerPath, sizeof(gerPath), "%s", detected.c_str());
    }

    bool isActionRunning() const { return false; }
    bool canStartAction() const { return gerPath[0] != '\0'; }
    void cancelAction(){}

    void startAction(){
        saveCurrentSection();
    }

    static std::vector<std::wstring> splitLines(const std::wstring& text){
        std::vector<std::wstring> lines;
        size_t start = 0;
        while(start <= text.size()){
            size_t end = text.find(L'\n', start);
            if(end == std::wstring::npos){
                std::wstring line = text.substr(start);
                if(!line.empty() && line.back() == L'\r') line.pop_back();
                lines.push_back(std::move(line));
                break;
            }
            std::wstring line = text.substr(start, end - start);
            if(!line.empty() && line.back() == L'\r') line.pop_back();
            lines.push_back(std::move(line));
            start = end + 1;
        }
        if(lines.empty()) lines.push_back(L"");
        return lines;
    }

    static std::wstring joinLines(const std::vector<std::wstring>& lines){
        std::wstring out;
        for(size_t i = 0; i < lines.size(); ++i){
            if(i) out += L"\r\n";
            out += lines[i];
        }
        if(!out.empty()) out += L"\r\n";
        return out;
    }

    static std::string sanitizePreviewText(const char* raw, const char* fallback){
        std::string text = raw ? raw : "";
        for(char& ch : text){
            if(ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
        }

        std::string collapsed;
        collapsed.reserve(text.size());
        bool prevSpace = true;
        for(char ch : text){
            const bool isSpace = ch == ' ';
            if(isSpace){
                if(!prevSpace) collapsed.push_back(' ');
            } else {
                collapsed.push_back(ch);
            }
            prevSpace = isSpace;
        }

        while(!collapsed.empty() && collapsed.front() == ' ') collapsed.erase(collapsed.begin());
        while(!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
        if(collapsed.empty()) collapsed = fallback ? fallback : "";
        return collapsed;
    }

    static std::string stripTags(const std::string& value){
        return std::regex_replace(value, std::regex("<[^>]*>"), "");
    }

    static float previewFontSize(float width, float height, const std::string& text){
        const float len = (float)std::max<size_t>(text.size(), 1);
        const float byHeight = height * 0.42f;
        const float byWidth = width / std::max(4.5f, len * 0.58f);
        return std::clamp(std::min(byHeight, byWidth), 20.f, 54.f);
    }

    const char* fontLabel(int idx) const{
        if(fonts.empty()) return "None";
        idx = std::clamp(idx, 0, (int)fonts.size() - 1);
        return fonts[idx].c_str();
    }

    void drawFontCombo(const char* id, int& fontIdx){
        ImGui::SetNextItemWidth(-FLT_MIN);
        if(fonts.empty()){
            ImGui::BeginDisabled();
            char none[] = "None";
            ImGui::InputText(id, none, sizeof(none), ImGuiInputTextFlags_ReadOnly);
            ImGui::EndDisabled();
            return;
        }

        fontIdx = std::clamp(fontIdx, 0, (int)fonts.size() - 1);
        if(ImGui::BeginCombo(id, fonts[fontIdx].c_str())){
            for(int i = 0; i < (int)fonts.size(); ++i){
                if(ImGui::Selectable(fonts[i].c_str(), fontIdx == i))
                    fontIdx = i;
                if(fontIdx == i) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    static void drawModeCombo(const char* id, TextStyleState& state){
        ImGui::SetNextItemWidth(-FLT_MIN);
        const int previewIdx = std::clamp(state.modeIdx, 0, 3);
        if(ImGui::BeginCombo(id, MODES[previewIdx])){
            for(int i = 0; i < 4; ++i){
                if(ImGui::Selectable(MODES[i], state.modeIdx == i))
                    state.modeIdx = i;
                if(state.modeIdx == i) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    static void drawColourControls(const char* prefix, TextStyleState& state){
        ImGui::PushID(prefix);
        if(state.modeIdx == 0){
            ColorPickerButton("##solid", state.solid);
        } else if(state.modeIdx == 1){
            for(int i = 0; i < 6; ++i){
                char id[16];
                std::snprintf(id, sizeof(id), "##step%d", i);
                ColorPickerButton(id, state.stepped[i], 22.f, 20.f);
                if(i < 5) ImGui::SameLine();
            }
        } else if(state.modeIdx == 2){
            ImGui::TextUnformatted("Start");
            ImGui::SameLine();
            ColorPickerButton("##smoothA", state.smoothStart);
            ImGui::SameLine();
            ImGui::TextUnformatted("End");
            ImGui::SameLine();
            ColorPickerButton("##smoothB", state.smoothEnd);
        } else {
            ImGui::TextUnformatted("1");
            ImGui::SameLine();
            ColorPickerButton("##tripleA", state.tripleStart);
            ImGui::SameLine();
            ImGui::TextUnformatted("2");
            ImGui::SameLine();
            ColorPickerButton("##tripleB", state.tripleMid);
            ImGui::SameLine();
            ImGui::TextUnformatted("3");
            ImGui::SameLine();
            ColorPickerButton("##tripleC", state.tripleEnd);
        }
        ImGui::PopID();
    }

    std::string buildFormattedValue(const TextStyleState& state, const char* fallbackText) const{
        const std::string plain = sanitizePreviewText(state.text, fallbackText);
        std::string coloured;
        if(state.modeIdx == 1){
            std::vector<RGB> palette;
            palette.reserve(6);
            for(const auto& step : state.stepped)
                palette.push_back(rgbFromFloats(step));
            coloured = hardGradientString(plain, palette, true);
        } else if(state.modeIdx == 2){
            coloured = smoothGradientString(plain, rgbFromFloats(state.smoothStart), rgbFromFloats(state.smoothEnd), true);
        } else if(state.modeIdx == 3){
            coloured = tripleGradientString(plain,
                rgbFromFloats(state.tripleStart),
                rgbFromFloats(state.tripleMid),
                rgbFromFloats(state.tripleEnd),
                true);
        } else {
            const RGB solid = rgbFromFloats(state.solid);
            coloured = apb::colorTag(solid, plain);
        }

        const char* font = fontLabel(state.fontIdx);
        if(font && std::strcmp(font, "None") != 0 && *font)
            return std::string("<Fonts:") + font + ">" + coloured;
        return coloured;
    }

    void setFontFromTag(TextStyleState& state, const std::string& tag){
        for(int i = 0; i < (int)fonts.size(); ++i){
            if(fonts[i] == tag){
                state.fontIdx = i;
                return;
            }
        }
        state.fontIdx = 0;
    }

    static bool isLinearGradient(const std::vector<RGB>& colours){
        if(colours.size() < 2) return false;
        const RGB start = colours.front();
        const RGB end = colours.back();
        for(size_t i = 0; i < colours.size(); ++i){
            const double t = colours.size() == 1 ? 0.0 : (double)i / (double)(colours.size() - 1);
            if(!sameRgb(colours[i], lerpRGB(start, end, t), 0.004f))
                return false;
        }
        return true;
    }

    static bool tryParseStepped(const std::vector<RGB>& colours, TextStyleState& state){
        if(colours.size() < 7) return false;
        const int maxPalette = std::min<int>(6, (int)colours.size());
        for(int paletteCount = maxPalette; paletteCount >= 2; --paletteCount){
            const std::vector<int> pattern = bouncePattern(paletteCount);
            bool matches = true;
            for(size_t i = 0; i < colours.size(); ++i){
                if(!sameRgb(colours[i], colours[pattern[i % pattern.size()]], 0.004f)){
                    matches = false;
                    break;
                }
            }
            if(!matches) continue;

            state.modeIdx = 1;
            for(int i = 0; i < 6; ++i){
                const RGB fill = colours[std::min(i, paletteCount - 1)];
                state.stepped[i][0] = (float)fill.r;
                state.stepped[i][1] = (float)fill.g;
                state.stepped[i][2] = (float)fill.b;
            }
            return true;
        }
        return false;
    }

    void applyLoadedValue(TextStyleState& state, const std::wstring& loadedValue, const char* defaultText){
        resetStyle(state, defaultText);
        std::string value = WideToUtf8(loadedValue);
        if(value.empty()) return;

        std::smatch fontMatch;
        const std::regex fontRx(R"(^<Fonts:([^>]+)>)", std::regex_constants::icase);
        if(std::regex_search(value, fontMatch, fontRx)){
            setFontFromTag(state, fontMatch[1].str());
            value = fontMatch.suffix().str();
        }

        const std::string plain = sanitizePreviewText(stripTags(value).c_str(), defaultText);
        std::snprintf(state.text, sizeof(state.text), "%s", plain.c_str());

        std::vector<RGB> colours;
        const std::regex colorRx(
            R"(<\s*Color\s*:\s*R\s*=\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s+G\s*=\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s+B\s*=\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s*>)",
            std::regex_constants::icase);
        for(std::sregex_iterator it(value.begin(), value.end(), colorRx), end; it != end; ++it){
            colours.push_back({
                std::stod((*it)[1].str()),
                std::stod((*it)[2].str()),
                std::stod((*it)[3].str())
            });
        }

        if(colours.empty()) return;

        bool same = true;
        for(size_t i = 1; i < colours.size(); ++i){
            if(!sameRgb(colours[i], colours.front())){
                same = false;
                break;
            }
        }
        if(same){
            state.modeIdx = 0;
            state.solid[0] = (float)colours.front().r;
            state.solid[1] = (float)colours.front().g;
            state.solid[2] = (float)colours.front().b;
            return;
        }

        if(tryParseStepped(colours, state))
            return;

        if(isLinearGradient(colours)){
            state.modeIdx = 2;
            state.smoothStart[0] = (float)colours.front().r;
            state.smoothStart[1] = (float)colours.front().g;
            state.smoothStart[2] = (float)colours.front().b;
            state.smoothEnd[0] = (float)colours.back().r;
            state.smoothEnd[1] = (float)colours.back().g;
            state.smoothEnd[2] = (float)colours.back().b;
            return;
        }

        state.modeIdx = 3;
        const RGB first = colours.front();
        const RGB middle = colours[colours.size() / 2];
        const RGB last = colours.back();
        state.tripleStart[0] = (float)first.r;
        state.tripleStart[1] = (float)first.g;
        state.tripleStart[2] = (float)first.b;
        state.tripleMid[0] = (float)middle.r;
        state.tripleMid[1] = (float)middle.g;
        state.tripleMid[2] = (float)middle.b;
        state.tripleEnd[0] = (float)last.r;
        state.tripleEnd[1] = (float)last.g;
        state.tripleEnd[2] = (float)last.b;
    }

    void ensurePreviewTexture(){
        if(backgroundSrv || !g_dev) return;
        namespace fs = std::filesystem;
        const char* fileName = "reload_resupply_preview.png";

        std::vector<fs::path> roots;
        char exeBuf[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exeBuf, MAX_PATH);
        fs::path exeDir = fs::path(exeBuf).parent_path();
        for(int i = 0; i < 4 && !exeDir.empty(); ++i){
            roots.push_back(exeDir);
            exeDir = exeDir.parent_path();
        }

        fs::path cwd = fs::current_path();
        for(int i = 0; i < 3 && !cwd.empty(); ++i){
            roots.push_back(cwd);
            cwd = cwd.parent_path();
        }

        roots.push_back(fs::path(apb::AssetsDir()));

        for(const auto& root : roots){
            const std::vector<fs::path> candidates = {
                root / "Assets" / "Images_UI" / fileName,
                root / "Images_UI" / fileName
            };
            for(const auto& candidate : candidates){
                if(apb::LoadTextureFromFile(g_dev, candidate.string(), &backgroundSrv, &backgroundW, &backgroundH))
                    return;
            }
        }
    }

    bool loadFromFile(){
        autoLoadAttempted = true;
        if(!gerPath[0]){
            log.append("GER file path is empty.");
            return false;
        }

        TextFileData data;
        if(!ReadTextFilePreserveEncoding(gerPath, data)){
            log.append("Failed to load APBUserInterface.GER.");
            return false;
        }

        fileEncoding = data.encoding;
        std::wstring reloadValue;
        std::wstring resupplyValue;
        bool foundReload = false;
        bool foundResupply = false;

        for(const std::wstring& line : splitLines(data.text)){
            if(line.rfind(L"Reload=", 0) == 0){
                reloadValue = line.substr(7);
                foundReload = true;
            } else if(line.rfind(L"Resupply=", 0) == 0){
                resupplyValue = line.substr(9);
                foundResupply = true;
            }
        }

        applyLoadedValue(reload, reloadValue, "Reload");
        applyLoadedValue(resupply, resupplyValue, "Resupply");
        log.append(std::string("Loaded ") + gerPath);
        if(!foundReload) log.append("Reload= entry not found.");
        if(!foundResupply) log.append("Resupply= entry not found.");
        return true;
    }

    bool saveEntries(bool saveReload, bool saveResupply){
        if(!gerPath[0]){
            log.append("GER file path is empty.");
            return false;
        }

        TextFileData data;
        const bool fileExists = ReadTextFilePreserveEncoding(gerPath, data);
        if(!fileExists){
            data.encoding = fileEncoding;
            data.text.clear();
        }

        std::vector<std::wstring> lines = splitLines(data.text);
        if(lines.size() == 1 && lines[0].empty() && data.text.empty()) lines.clear();

        const std::wstring reloadLine = L"Reload=" + Utf8ToWide(buildFormattedValue(reload, "Reload"));
        const std::wstring resupplyLine = L"Resupply=" + Utf8ToWide(buildFormattedValue(resupply, "Resupply"));
        bool foundReload = false;
        bool foundResupply = false;

        for(std::wstring& line : lines){
            if(saveReload && line.rfind(L"Reload=", 0) == 0){
                line = reloadLine;
                foundReload = true;
            } else if(saveResupply && line.rfind(L"Resupply=", 0) == 0){
                line = resupplyLine;
                foundResupply = true;
            }
        }

        if(saveReload && !foundReload) lines.push_back(reloadLine);
        if(saveResupply && !foundResupply) lines.push_back(resupplyLine);

        data.encoding = fileExists ? data.encoding : fileEncoding;
        data.text = joinLines(lines);
        if(!WriteTextFilePreserveEncoding(gerPath, data)){
            log.append("Failed to save APBUserInterface.GER.");
            return false;
        }

        lastSavedPath = gerPath;
        autoLoadAttempted = true;
        if(saveReload && saveResupply) log.append(std::string("Saved Reload and Resupply in ") + gerPath);
        else if(saveReload) log.append(std::string("Saved Reload in ") + gerPath);
        else if(saveResupply) log.append(std::string("Saved Resupply in ") + gerPath);
        return true;
    }

    bool saveCurrentSection(){
        return activeTabIdx == 0
            ? saveEntries(true, false)
            : saveEntries(false, true);
    }

    void tryAutoLoad(){
        if(autoLoadAttempted || !gerPath[0]) return;
        loadFromFile();
    }

    static ImU32 previewColor(const RGB& rgb){
        return IM_COL32(
            std::clamp((int)std::lround(rgb.r * 255.0), 0, 255),
            std::clamp((int)std::lround(rgb.g * 255.0), 0, 255),
            std::clamp((int)std::lround(rgb.b * 255.0), 0, 255),
            204);
    }

    std::vector<ImU32> buildPreviewPalette(const TextStyleState& state, const std::string& text) const{
        int visibleChars = 0;
        for(char ch : text)
            if(ch != ' ') ++visibleChars;

        std::vector<ImU32> out;
        out.reserve((size_t)visibleChars);
        if(visibleChars == 0) return out;

        if(state.modeIdx == 1){
            const std::vector<int> pattern = bouncePattern(6);
            for(int i = 0; i < visibleChars; ++i)
                out.push_back(previewColor(rgbFromFloats(state.stepped[pattern[i % pattern.size()]])));
            return out;
        }

        if(state.modeIdx == 2){
            const RGB start = rgbFromFloats(state.smoothStart);
            const RGB end = rgbFromFloats(state.smoothEnd);
            for(int i = 0; i < visibleChars; ++i){
                const double t = visibleChars == 1 ? 0.0 : (double)i / (double)(visibleChars - 1);
                out.push_back(previewColor(lerpRGB(start, end, t)));
            }
            return out;
        }

        if(state.modeIdx == 3){
            const RGB start = rgbFromFloats(state.tripleStart);
            const RGB middle = rgbFromFloats(state.tripleMid);
            const RGB end = rgbFromFloats(state.tripleEnd);
            for(int i = 0; i < visibleChars; ++i){
                const double t = visibleChars == 1 ? 0.0 : (double)i / (double)(visibleChars - 1);
                const RGB rgb = t <= 0.5
                    ? lerpRGB(start, middle, t * 2.0)
                    : lerpRGB(middle, end, (t - 0.5) * 2.0);
                out.push_back(previewColor(rgb));
            }
            return out;
        }

        out.assign((size_t)visibleChars, previewColor(rgbFromFloats(state.solid)));
        return out;
    }

    void drawPreview(const TextStyleState& state, const char* fallbackText){
        ensurePreviewTexture();

        const std::string previewText = sanitizePreviewText(state.text, fallbackText);

        const float availW = ImGui::GetContentRegionAvail().x;
        const float previewW = std::min(availW, 760.f);
        const float fallbackH = previewW * (129.f / 499.f);
        const float previewH = backgroundSrv && backgroundW > 0
            ? previewW * ((float)backgroundH / (float)backgroundW)
            : fallbackH;

        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if(backgroundSrv){
            dl->AddImage((ImTextureID)backgroundSrv, pos, {pos.x + previewW, pos.y + previewH});
        } else {
            dl->AddRectFilled(pos, {pos.x + previewW, pos.y + previewH}, IM_COL32(34, 34, 34, 255));
            dl->AddRect(pos, {pos.x + previewW, pos.y + previewH}, IM_COL32(90, 90, 90, 255));
        }

        float selectedPixelSize = 12.f;
        ImFont* font = ResolvePreviewFont(fontLabel(state.fontIdx), &selectedPixelSize);
        if(!font) font = ImGui::GetFont();

        float fontSize = previewFontSize(previewW, previewH, previewText);
        if(selectedPixelSize > 0.f)
            fontSize = std::clamp(fontSize * (selectedPixelSize / 14.f), 18.f, 72.f);

        const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, previewText.c_str());
        const ImVec2 textPos = {
            pos.x + (previewW - textSize.x) * 0.5f,
            pos.y + (previewH - textSize.y) * 0.5f
        };

        const std::vector<ImU32> palette = buildPreviewPalette(state, previewText);
        float x = textPos.x;
        size_t visibleIdx = 0;
        for(char ch : previewText){
            char buf[2] = {ch, '\0'};
            if(ch != ' ' && visibleIdx < palette.size())
                dl->AddText(font, fontSize, {x, textPos.y}, palette[visibleIdx++], buf);
            x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, buf).x;
        }
        ImGui::Dummy({previewW, previewH});
    }

    void drawEntryEditor(const char* tabId, const char* title, TextStyleState& state, const char* fallbackText){
        SectionLabel(title);
        if(BeginSectionTable(tabId, 124.f, 110.f)){
            BeginSectionRow("Text");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##rrtext", state.text, sizeof(state.text));

            BeginSectionRow("Mode");
            drawModeCombo("##rrmode", state);

            BeginSectionRow("Colours");
            drawColourControls("rrcolours", state);

            BeginSectionRow("Font");
            drawFontCombo("##rrfont", state.fontIdx);
            EndSectionTable();
        }

        SectionLabel("Preview");
        if(BeginSectionTable("##rrpreviewgrid", 124.f, 110.f)){
            BeginSectionRow("");
            ImGui::Dummy({0.f, 0.f});
            NextSectionAction();
            ImGui::PushStyleColor(ImGuiCol_Button,        Col::BTN_OK);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Col::YELLOW_DIM);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Col::YELLOW_DIM);
            ImGui::PushStyleColor(ImGuiCol_Text,          {0.04f,0.04f,0.04f,1.f});
            ImGui::PushStyleColor(ImGuiCol_Border,        Col::YELLOW_DIM);
            if(ImGui::Button("Save##rrsave", {86.f, 32.f})) saveCurrentSection();
            ImGui::PopStyleColor(5);
            EndSectionTable();
        }
        drawPreview(state, fallbackText);
        SectionNote("Preview uses embedded Helvetica faces for mapped APB font tags. None keeps save output tag-free.");
    }

    void draw(){
        ensureDefaultGerPath();
        tryAutoLoad();

        ImGui::BeginChild("##reloadresupply", {0, 0}, false);
        SectionLabel("Reload / Resupply Text");
        SectionNote("Edit the APBUserInterface.GER Reload and Resupply entries and preview them against the in-game background.");

        SectionLabel("File");
        if(BeginSectionTable("##rrfilegrid", 124.f, 110.f)){
            BeginSectionRow("GER File");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if(ImGui::InputText("##rrgerpath", gerPath, MAX_PATH)){
                autoLoadAttempted = true;
            }
            NextSectionAction();
            if(ImGui::Button("Browse##rrger")){
                std::string s;
                if(BrowseFile(s, "GER Files\0*.ger;*.GER\0All Files\0*.*\0\0")){
                    std::snprintf(gerPath, sizeof(gerPath), "%s", s.c_str());
                    autoLoadAttempted = false;
                }
            }

            BeginSectionRow("Actions");
            if(ImGui::Button("Auto Detect##rrdetect")){
                gerPath[0] = '\0';
                autoDetectTried = false;
                autoLoadAttempted = false;
                applySavedTabSelection = true;
                resetStyle(reload, "Reload");
                resetStyle(resupply, "Resupply");
                ensureDefaultGerPath();
                tryAutoLoad();
                if(!gerPath[0]) log.append("APBUserInterface.GER was not auto-detected.");
            }
            ImGui::SameLine();
            if(ImGui::Button("Load##rrload")) loadFromFile();
            ImGui::SameLine();
            if(gerPath[0] && ImGui::Button("Open##rropen")) OpenInExplorer(gerPath);
            EndSectionTable();
        }

        if(ImGui::BeginTabBar("##rrtabs")){
            if(ImGui::BeginTabItem("Reload", nullptr,
                (applySavedTabSelection && activeTabIdx == 0) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)){
                activeTabIdx = 0;
                applySavedTabSelection = false;
                drawEntryEditor("##rrreloadgrid", "Reload", reload, "Reload");
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Resupply", nullptr,
                (applySavedTabSelection && activeTabIdx == 1) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)){
                activeTabIdx = 1;
                applySavedTabSelection = false;
                drawEntryEditor("##rrresupplygrid", "Resupply", resupply, "Resupply");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        SectionLabel("Log");
        std::string t = log.get();
        ReadOnlyLogBox("##rrlog", t, {-1.f, -1.f});
        ImGui::EndChild();
    }
};

// ══════════════════════════════════════════════════════════════════════════
// PageWeaponItemTypes
// ══════════════════════════════════════════════════════════════════════════
struct PageWeaponItemTypes {
    char   intPath[MAX_PATH]={}, outPath[MAX_PATH]={};
    Progress prog;
    ThreadLog log;
    std::atomic<bool> running{false};
    std::atomic<bool> cancelRequested{false};
    std::string lastOut;
    ColourSchemeWidget colours;
    bool   autoDetectTried=false;

    // Preview cards
    struct WCard { std::string name; PreviewCard card; std::vector<std::pair<std::string,std::string>> stats; };
    std::vector<WCard> cards;
    bool cardsInit=false;

    void initCards(){
        if(cardsInit) return; cardsInit=true;
        std::string dir = WeaponImgDir();
        cards = {
            {"UL-3 'Jersey Devil'", {}, {{"Time To Kill","0.756 sec"},{"Shots To Kill","13"},{"Health Damage","80"},{"Stamina Damage","20"},{"Hard Damage","7.2"},{"Effective Range","30 m"},{"Fire Interval","0.063 sec"},{"Reload Time","1.5 sec"},{"Equip Time","0.366 sec"}}},
            {"Stabba NL9",          {}, {{"Time to Stun","1.56 sec"},{"Shots To Stun","3"},{"Health Damage","200"},{"Stamina Damage","400"},{"Hard Damage","66.0"},{"Effective Range","50 m"},{"Fire Interval","0.78 sec"},{"Reload Time","0.7 sec"},{"Equip Time","0.9 sec"}}},
            {"OSMAW",               {}, {{"Time To Kill","1.75 sec"},{"Shots To Kill","1"},{"Max Health Damage","1000"},{"Max Stamina Damage","300"},{"Max Hard Damage","1227.6"},{"Wind Up Time","1.75 sec"},{"Reload Time","2.25 sec"},{"Equip Time","0.7 sec"},{"Air Burst Distance","142.5 m"}}},
            {"Frag Grenade",        {}, {{"Max Health Damage","750"},{"Max Stamina Damage","375"},{"Max Hard Damage","567.0"},{"Explosion Radius","700 cm"},{"Max Damage Radius","300 cm"},{"Fuse Delay","4 sec"},{"Speed","15.5 m/s"}}},
        };
        cards[0].card.imgPath = dir+"\\ul3_weapon_desc.png";
        cards[1].card.imgPath = dir+"\\stabbanl9_weapon_desc.png";
        cards[2].card.imgPath = dir+"\\osmaw_weapon_desc.png";
        cards[3].card.imgPath = dir+"\\frag_weapon_desc.png";
        cards[0].card.resourceId = IDR_IMG_WEAPON_UL3;
        cards[1].card.resourceId = IDR_IMG_WEAPON_STABBANL9;
        cards[2].card.resourceId = IDR_IMG_WEAPON_OSMAW;
        cards[3].card.resourceId = IDR_IMG_WEAPON_FRAG;
        for(auto& c:cards) c.card.tryLoad();
    }

    PreviewTextGradient keyGradient() const {
        auto toU32=[](const RGB& c){
            return IM_COL32((int)(c.r*255),(int)(c.g*255),(int)(c.b*255),255);
        };
        auto solid=[](ImU32 c){ return PreviewTextGradient{c,c,c,false}; };
        if(colours.schemeIdx==0 || colours.schemeIdx==1)
            return solid(IM_COL32(229,229,229,255));
        if(colours.schemeIdx==2){
            auto& c=colours.singleCol;
            ImU32 col=IM_COL32((int)(c[0]*255),(int)(c[1]*255),(int)(c[2]*255),255);
            return solid(col);
        }
        if(!colours.presetEquipped && colours.schemeIdx==4){
            RGB gs,gm,ge; colours.resolvedTriple(gs,gm,ge);
            return {toU32(gs),toU32(gm),toU32(ge),true};
        }
        RGB gs,ge; colours.resolvedGrad(gs,ge);
        return {toU32(gs),toU32(gs),toU32(ge),false};
    }

    void ensureDefaultIntPath(){
        if(autoDetectTried || intPath[0]) return;
        autoDetectTried = true;
        const std::string detected = DetectApbIntFile("WeaponItemTypes.INT");
        if(!detected.empty()) strncpy(intPath, detected.c_str(), MAX_PATH - 1);
    }

    bool isActionRunning() const { return running.load(); }
    bool canStartAction() const { return !running.load() && intPath[0]; }

    void startAction(){
        if(!canStartAction()) return;
        log.clear();
        cancelRequested = false;
        running = true;
        lastOut.clear();
        prog.reset(0);
        std::string ip=intPath,od=outPath;
        constexpr int wk=8;
        Scheme sc; RGB si,gs,ge; colours.getScheme(sc,si,gs,ge);
        std::thread([this,ip,od,sc,si,gs,ge,wk](){
            try{
                namespace fs = std::filesystem;
                fs::path inP(ip);
                fs::path outDir = od.empty() ? fs::path(DownloadsDir()) : fs::path(od);
                fs::create_directories(outDir);
                fs::path outFile = outDir / (inP.stem().string() + "_Generated.INT");
                bool completed = generateWeaponDescriptionsFile(ip,outFile.string(),sc,si,gs,ge,15,wk,
                    [this](int d,int t,const std::string& k){prog.set(d,t,k);},
                    &cancelRequested);
                if(completed){
                    lastOut=outFile.string();
                    log.append("Done \xe2\x86\x92 "+lastOut);
                } else {
                    log.append("Cancelled.");
                }
            }catch(std::exception& e){log.append(std::string("Error: ")+e.what());}
            cancelRequested = false;
            running = false;
        }).detach();
    }

    void cancelAction(){
        if(!running.load()) return;
        cancelRequested = true;
        log.append("Cancelling...");
    }

    void draw(){
        initCards();
        ensureDefaultIntPath();
        ImGui::BeginChild("##wit",{0,0},false);
        SectionLabel("WeaponItemTypes – Stat Generation");
        SectionNote("Generate formatted weapon descriptions from an ItemTypes INT file and preview the selected stat style before writing output.");

        SectionLabel("Files");
        if(BeginSectionTable("##witfiles", 116.f, 86.f)){
            BeginSectionRow("INT File");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##witint",intPath,MAX_PATH);
            NextSectionAction();
            if(ImGui::Button("Browse##wit")){
                std::string s;
                if(BrowseFile(s,"INT Files\0*.int;*.INT\0\0")) strncpy(intPath,s.c_str(),MAX_PATH-1);
            }

            BeginSectionRow("Output Folder");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##witout",outPath,MAX_PATH);
            NextSectionAction();
            if(ImGui::Button("Browse##witout")){
                std::string s;
                if(BrowseFolder(s,"Select output folder")) strncpy(outPath,s.c_str(),MAX_PATH-1);
            }
            EndSectionTable();
        }
        SectionNote("Leave Output Folder blank to write the generated file to Downloads.");

        SectionLabel("Appearance");
        bool colChanged=false;
        colours.draw("wit",colChanged);

        SectionLabel("Progress");
        ImGui::ProgressBar(prog.frac(),{-1,18});
        ImGui::TextColored(Col::SUBTEXT,"%d / %d  %s",prog.done.load(),prog.total.load(),prog.lbl().c_str());

        bool busy=running.load();
        if(busy) ImGui::BeginDisabled();
        if(RunButton("Run##wit")) startAction();
        if(busy) ImGui::EndDisabled();
        ImGui::SameLine();
        if(!lastOut.empty()&&ImGui::Button("Open Output",{110,32})) OpenInExplorer(lastOut);

        ImGui::Spacing();

        // ── Preview cards grid (2×2) ──────────────────────────────────
        SectionLabel("Preview");
        PreviewTextGradient kg = keyGradient();
        ImU32 vc = IM_COL32(255,255,255,255);
        float cardW  = 320.f;
        float colGap = 16.f;
        float rowStep = cardW * (362.f / 327.f) + 14.f;

        ImGui::BeginChild("##witpreview",{0,760.f},false);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 win = ImGui::GetWindowPos();
        float cx = win.x + 4.f;
        float cy = win.y + 4.f;
        bool showStats = (colours.schemeIdx != 0);
        auto wrapDescription = [](const std::string& desc, int maxChars = 46){
            std::vector<std::pair<std::string,std::string>> rows;
            size_t i = 0;
            while(i < desc.size()){
                while(i < desc.size() && desc[i] == ' ') ++i;
                if(i >= desc.size()) break;
                size_t lineEnd = std::min(desc.size(), i + (size_t)maxChars);
                size_t cut = lineEnd;
                if(lineEnd < desc.size()){
                    size_t sp = desc.rfind(' ', lineEnd);
                    if(sp != std::string::npos && sp > i) cut = sp;
                }
                if(cut <= i) cut = lineEnd;
                rows.push_back({"", desc.substr(i, cut - i)});
                i = cut;
            }
            return rows;
        };
        static const char* VANILLA_DESC[4] = {
            "Configured with the SPEED DEMON SPECIAL which allows you to draw quicker and fire faster, this gun can be called for quickly to rain down hell fire just as fast. The Jersey Devil model is just the curse you need for those wanting speed in every way possible.",
            "The Stabba less than lethal shotgun fires a custom made electrical dart that can stun a target at medium ranges, though it will lose it's charge quickly over range. Initially shelved due to accidental deaths during testing, this weapon has been brought back into service after protocol changes deemed the percentages acceptable.",
            "The Obeya Shoulder-launched Multi-purpose Assault Weapon has long been thought as of overkill in an urban environment. After Armas brought it into the Underground markets however, it wasn't long before both sides were forced to supply their forces with them to keep up. Downsides include a very limited ammo pool and slow reload times.",
            "Fragmentation grenades cause injury and collateral damage, dispersed over a wide area."
        };
        static const int VANILLA_PAD_LINES[4] = {0,0,0,0};
        static const int VANILLA_WRAP_CHARS[4] = {40,46,46,46};
        static const float VANILLA_Y_OFFSET[4] = {3.f,3.f,3.f,-60.f};
        static const float VANILLA_X_OFFSET_PCT = -0.01f;
        for(int i=0;i<(int)cards.size();++i){
            cards[i].card.tryLoad(); // retry each frame until success
            float px = cx + (i%2)*(cardW+colGap);
            float py = cy + (i/2)*rowStep;
            if(showStats){
                float statsY = VANILLA_Y_OFFSET[i];
                float statsX = cardW * VANILLA_X_OFFSET_PCT - 5.f;
                cards[i].card.draw(dl,{px,py},cardW,cards[i].stats,kg,vc,statsY,statsX);
            } else {
                auto vanillaRows = wrapDescription(VANILLA_DESC[i], VANILLA_WRAP_CHARS[i]);
                for(int p=0; p<VANILLA_PAD_LINES[i]; ++p)
                    vanillaRows.insert(vanillaRows.begin(), {"",""});
                cards[i].card.draw(dl,{px,py},cardW,vanillaRows,kg,vc,VANILLA_Y_OFFSET[i], cardW * VANILLA_X_OFFSET_PCT - 5.f);
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();
        std::string t=log.get();
        SectionLabel("Log");
        ReadOnlyLogBox("##witlog", t, {-1.f, 100.f});
        ImGui::EndChild();
    }
};

// ══════════════════════════════════════════════════════════════════════════
// PageVehicleItemTypes
// ══════════════════════════════════════════════════════════════════════════
struct PageVehicleItemTypes {
    char   intPath[MAX_PATH]={}, outPath[MAX_PATH]={};
    Progress prog;
    ThreadLog log;
    std::atomic<bool> running{false};
    std::atomic<bool> cancelRequested{false};
    std::string lastOut;
    ColourSchemeWidget colours;
    bool   autoDetectTried=false;

    struct VCard { std::string name; PreviewCard card; std::vector<std::pair<std::string,std::string>> stats; };
    std::vector<VCard> cards;
    bool cardsInit=false;

    void initCards(){
        if(cardsInit) return; cardsInit=true;
        std::string dir = VehicleImgDir();
        cards = {
            {"Han Coywolf CR4",        {}, {{"Max Health","990"},{"Max Speed","20.1 m/s"},{"Max Reverse Speed","12 m/s"},{"Cargo Capacity","5"},{"Vehicle Weight","2.4"},{"Explosion Max Damage","1400"},{"Explosion Radius","750 cm"}}},
            {"Joker Vegas G24 4x4",    {}, {{"Max Health","1150"},{"Max Speed","22 m/s"},{"Max Reverse Speed","12 m/s"},{"Cargo Capacity","5"},{"Vehicle Weight","4.0"},{"Explosion Max Damage","1400"},{"Explosion Radius","750 cm"}}},
            {"Nulander Nomad Q134",    {}, {{"Max Health","1500"},{"Max Speed","20.9 m/s"},{"Max Reverse Speed","12 m/s"},{"Cargo Capacity","15"},{"Vehicle Weight","5.5"},{"Explosion Max Damage","2000"},{"Explosion Radius","850 cm"}}},
            {"Sungnyemun Mirage S-24", {}, {{"Max Health","1050"},{"Max Speed","22.5 m/s"},{"Max Reverse Speed","12 m/s"},{"Cargo Capacity","5"},{"Vehicle Weight","2.4"},{"Explosion Max Damage","1400"},{"Explosion Radius","750 cm"}}},
        };
        cards[0].card.imgPath = dir+"\\coywolf.png";
        cards[1].card.imgPath = dir+"\\vegas.png";
        cards[2].card.imgPath = dir+"\\pioneer.png";
        cards[3].card.imgPath = dir+"\\mirage.png";
        cards[0].card.resourceId = IDR_IMG_VEHICLE_COYWOLF;
        cards[1].card.resourceId = IDR_IMG_VEHICLE_VEGAS;
        cards[2].card.resourceId = IDR_IMG_VEHICLE_PIONEER;
        cards[3].card.resourceId = IDR_IMG_VEHICLE_MIRAGE;
        for(auto& c:cards) c.card.tryLoad();
    }

    PreviewTextGradient keyGradient() const {
        auto toU32=[](const RGB& c){
            return IM_COL32((int)(c.r*255),(int)(c.g*255),(int)(c.b*255),255);
        };
        auto solid=[](ImU32 c){ return PreviewTextGradient{c,c,c,false}; };
        if(colours.schemeIdx==0 || colours.schemeIdx==1)
            return solid(IM_COL32(229,229,229,255));
        if(colours.schemeIdx==2){
            auto& c=colours.singleCol;
            ImU32 col=IM_COL32((int)(c[0]*255),(int)(c[1]*255),(int)(c[2]*255),255);
            return solid(col);
        }
        if(!colours.presetEquipped && colours.schemeIdx==4){
            RGB gs,gm,ge; colours.resolvedTriple(gs,gm,ge);
            return {toU32(gs),toU32(gm),toU32(ge),true};
        }
        RGB gs,ge; colours.resolvedGrad(gs,ge);
        return {toU32(gs),toU32(gs),toU32(ge),false};
    }

    void ensureDefaultIntPath(){
        if(autoDetectTried || intPath[0]) return;
        autoDetectTried = true;
        const std::string detected = DetectApbIntFile("VehicleItemTypes.INT");
        if(!detected.empty()) strncpy(intPath, detected.c_str(), MAX_PATH - 1);
    }

    bool isActionRunning() const { return running.load(); }
    bool canStartAction() const { return !running.load() && intPath[0]; }

    void startAction(){
        if(!canStartAction()) return;
        log.clear();
        cancelRequested = false;
        running = true;
        lastOut.clear();
        prog.reset(0);
        std::string ip=intPath,od=outPath;
        constexpr int wk=8;
        Scheme sc; RGB si,gs,ge; colours.getScheme(sc,si,gs,ge);
        std::thread([this,ip,od,sc,si,gs,ge,wk](){
            try{
                namespace fs = std::filesystem;
                fs::path inP(ip);
                fs::path outDir = od.empty() ? fs::path(DownloadsDir()) : fs::path(od);
                fs::create_directories(outDir);
                fs::path outFile = outDir / (inP.stem().string() + "_Generated.GER");
                bool completed = generateVehicleDescriptionsFile(ip,outFile.string(),sc,si,gs,ge,15,wk,
                    [this](int d,int t,const std::string& k){prog.set(d,t,k);},
                    &cancelRequested);
                if(completed){
                    lastOut=outFile.string();
                    log.append("Done \xe2\x86\x92 "+lastOut);
                } else {
                    log.append("Cancelled.");
                }
            }catch(std::exception& e){log.append(std::string("Error: ")+e.what());}
            cancelRequested = false;
            running = false;
        }).detach();
    }

    void cancelAction(){
        if(!running.load()) return;
        cancelRequested = true;
        log.append("Cancelling...");
    }

    void draw(){
        initCards();
        ensureDefaultIntPath();
        ImGui::BeginChild("##vit",{0,0},false);
        SectionLabel("VehicleItemTypes – Stat Generation");
        SectionNote("Generate formatted vehicle descriptions from an ItemTypes INT file and preview the selected stat style before writing output.");

        SectionLabel("Files");
        if(BeginSectionTable("##vitfiles", 116.f, 86.f)){
            BeginSectionRow("INT File");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##vitint",intPath,MAX_PATH);
            NextSectionAction();
            if(ImGui::Button("Browse##vit")){
                std::string s;
                if(BrowseFile(s,"INT Files\0*.int;*.INT\0\0"))strncpy(intPath,s.c_str(),MAX_PATH-1);
            }

            BeginSectionRow("Output Folder");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##vitout",outPath,MAX_PATH);
            NextSectionAction();
            if(ImGui::Button("Browse##vitout")){
                std::string s;
                if(BrowseFolder(s,"Select output folder")) strncpy(outPath,s.c_str(),MAX_PATH-1);
            }
            EndSectionTable();
        }
        SectionNote("Leave Output Folder blank to write the generated file to Downloads.");

        SectionLabel("Appearance");
        bool colChanged=false;
        colours.draw("vit",colChanged);

        SectionLabel("Progress");
        ImGui::ProgressBar(prog.frac(),{-1,18});
        ImGui::TextColored(Col::SUBTEXT,"%d / %d  %s",prog.done.load(),prog.total.load(),prog.lbl().c_str());

        bool busy=running.load();
        if(busy) ImGui::BeginDisabled();
        if(RunButton("Run##vit")) startAction();
        if(busy) ImGui::EndDisabled();
        ImGui::SameLine();
        if(!lastOut.empty()&&ImGui::Button("Open Output",{110,32})) OpenInExplorer(lastOut);

        ImGui::Spacing();

        // ── Preview cards grid ────────────────────────────────────────
        SectionLabel("Preview");
        PreviewTextGradient kg = keyGradient();
        ImU32 vc = IM_COL32(255,255,255,255);
        float cardW  = 320.f;
        float colGap = 16.f;
        float rowStep = cardW * (362.f / 327.f) + 14.f;

        ImGui::BeginChild("##vitpreview",{0,760.f},false);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 win = ImGui::GetWindowPos();
        float cx = win.x + 4.f;
        float cy = win.y + 4.f;
        bool showStats = (colours.schemeIdx != 0);
        auto wrapDescription = [](const std::string& desc, int maxChars = 46){
            std::vector<std::pair<std::string,std::string>> rows;
            size_t i = 0;
            while(i < desc.size()){
                while(i < desc.size() && desc[i] == ' ') ++i;
                if(i >= desc.size()) break;
                size_t lineEnd = std::min(desc.size(), i + (size_t)maxChars);
                size_t cut = lineEnd;
                if(lineEnd < desc.size()){
                    size_t sp = desc.rfind(' ', lineEnd);
                    if(sp != std::string::npos && sp > i) cut = sp;
                }
                if(cut <= i) cut = lineEnd;
                rows.push_back({"", desc.substr(i, cut - i)});
                i = cut;
            }
            return rows;
        };
        static const char* VANILLA_DESC[4] = {
            "Combining high acceleration with superb handling, the Han COYWOLF is designed and built to be a predator on and off the streets. This variant has space for four modifications.",
            "Jacked up by Ophelia, this Classic Muscle car has raised suspension and all wheel drive for better overall handling. Powering all wheels comes at a cost however, resulting in a slightly reduced maximum speed. It also has a chassis capable of four custom modifications.",
            "One of the toughest vehicles in San Paro. Features good grip, reasonable cargo space and excellent acceleration once it's on the move. Enforcer only.",
            "Combining quick acceleration with moderate handling, the Sungnyemun Mirage S-22 is designed and built to be a quick escape car."
        };
        // Mirage title wraps to 2 lines in the source image, so description body starts lower.
        static const int VANILLA_PAD_LINES[4] = {0,0,0,1};
        static const float VANILLA_Y_OFFSET[4] = {3.f,3.f,3.f,8.f};
        static const float VANILLA_X_OFFSET_PCT = -0.01f;
        for(int i=0;i<(int)cards.size();++i){
            cards[i].card.tryLoad();
            float px = cx + (i%2)*(cardW+colGap);
            float py = cy + (i/2)*rowStep;
            if(showStats){
                float statsY = VANILLA_Y_OFFSET[i];
                float statsX = cardW * VANILLA_X_OFFSET_PCT - 5.f;
                cards[i].card.draw(dl,{px,py},cardW,cards[i].stats,kg,vc,statsY,statsX);
            } else {
                auto vanillaRows = wrapDescription(VANILLA_DESC[i]);
                for(int p=0; p<VANILLA_PAD_LINES[i]; ++p)
                    vanillaRows.insert(vanillaRows.begin(), {"",""});
                cards[i].card.draw(dl,{px,py},cardW,vanillaRows,kg,vc,VANILLA_Y_OFFSET[i], cardW * VANILLA_X_OFFSET_PCT - 5.f);
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();
        std::string t=log.get();
        SectionLabel("Log");
        ReadOnlyLogBox("##vitlog", t, {-1.f, 100.f});
        ImGui::EndChild();
    }
};

// ══════════════════════════════════════════════════════════════════════════
// PageArmasScrape
// ══════════════════════════════════════════════════════════════════════════
struct PageArmasScrape {
    int  startId=0, endId=25000, threads=32;
    char outPath[MAX_PATH]={};
    struct Hit{ int pid; std::string url,title; };
    std::vector<Hit> hits;
    std::mutex hitMu;
    Progress prog;
    std::atomic<bool> running{false};
    ArmasScraper* scraper=nullptr;
    std::mutex scraperMu;

    bool isActionRunning() const { return running.load(); }
    bool canStartAction() const { return !running.load(); }

    void startAction(){
        if(!canStartAction()) return;
        {std::lock_guard<std::mutex>lk(hitMu); hits.clear();}
        prog.reset(endId-startId+1);
        running=true;
        ScrapeConfig cfg; cfg.startId=startId; cfg.endId=endId; cfg.threads=threads; cfg.outPath=outPath;
        std::thread([this,cfg](){
            {
                std::lock_guard<std::mutex>lk(scraperMu);
                scraper=new ArmasScraper(cfg);
            }
            scraper->run(
                [this](int d,int t,const std::string& s){prog.set(d,t,s);},
                [this](int pid,const std::string& url,const std::string& title){
                    std::lock_guard<std::mutex>lk(hitMu); hits.push_back({pid,url,title});
                });
            {
                std::lock_guard<std::mutex>lk(scraperMu);
                delete scraper;
                scraper=nullptr;
            }
            running=false;
        }).detach();
    }

    void cancelAction(){
        std::lock_guard<std::mutex>lk(scraperMu);
        if(scraper) scraper->stop();
    }

    void draw(){
        ImGui::BeginChild("##as",{0,0},false);
        SectionLabel("ARMAS Product ID Scanner");
        SectionNote("Scan a product ID range and capture any matching marketplace entries to a report file.");

        SectionLabel("Scan Setup");
        if(BeginSectionTable("##assetup", 116.f, 86.f)){
            BeginSectionRow("Start ID");
            ImGui::SetNextItemWidth(120.f);
            ImGui::InputInt("##asst",&startId);

            BeginSectionRow("End ID");
            ImGui::SetNextItemWidth(120.f);
            ImGui::InputInt("##asen",&endId);

            BeginSectionRow("Threads");
            ImGui::SetNextItemWidth(120.f);
            ImGui::InputInt("##asth",&threads);
            threads=std::max(1,std::min(128,threads));

            BeginSectionRow("Output");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##asop",outPath,MAX_PATH);
            NextSectionAction();
            if(ImGui::Button("Browse##as")){
                std::string s;
                if(BrowseSaveFile(s,"Text Files\0*.txt\0\0","txt"))strncpy(outPath,s.c_str(),MAX_PATH-1);
            }
            EndSectionTable();
        }

        SectionLabel("Progress");
        ImGui::ProgressBar(prog.frac(),{-1,18});
        ImGui::TextColored(Col::SUBTEXT,"%d / %d  %s",prog.done.load(),prog.total.load(),prog.lbl().c_str());
        bool busy=running.load();
        if(!busy){
            if(RunButton("Start##as")) startAction();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,Col::RED);
            if(ImGui::Button("Stop",{120,32})) cancelAction();
            ImGui::PopStyleColor();
            ImGui::SameLine(); ImGui::TextColored(Col::YELLOW,"Scanning...");
        }
        SectionLabel("Results");
        ImGui::TextColored(Col::SUBTEXT,"Hits: %d",(int)hits.size());
        ImGui::Spacing();
        if(ImGui::BeginTable("##ashits",3,ImGuiTableFlags_Borders|ImGuiTableFlags_ScrollY|ImGuiTableFlags_RowBg,{0,0})){
            ImGui::TableSetupColumn("ID",    ImGuiTableColumnFlags_WidthFixed,60);
            ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("URL",   ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            std::lock_guard<std::mutex>lk(hitMu);
            for(auto& h:hits){
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%d",h.pid);
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(h.title.c_str());
                ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(h.url.c_str());
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
    }
};

// ══════════════════════════════════════════════════════════════════════════
// PageHexConverter
// ══════════════════════════════════════════════════════════════════════════
struct PageHexConverter {
    char apbTag[4096] = "<Color:R={number} G={number} B={number}>";
    float rgb[3] = {1.f, 0.f, 0.f};
    std::string output = "#FF0000";

    static int toByte(double value){
        if(value <= 1.0) value *= 255.0;
        return std::clamp((int)std::lround(value), 0, 255);
    }

    static std::string toHex(double r, double g, double b){
        std::ostringstream ss;
        ss << '#'
           << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << toByte(r)
           << std::setw(2) << toByte(g)
           << std::setw(2) << toByte(b);
        return ss.str();
    }

    static std::string convertTags(const char* text){
        static const std::regex tagPattern(
            R"(<\s*Color\s*:\s*R\s*=\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s+G\s*=\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s+B\s*=\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s*>)",
            std::regex_constants::icase);

        std::string source = text ? text : "";
        std::string result;
        for(std::sregex_iterator it(source.begin(), source.end(), tagPattern), end; it != end; ++it){
            const std::smatch& match = *it;
            const std::string hex = toHex(
                std::stod(match[1].str()),
                std::stod(match[2].str()),
                std::stod(match[3].str()));
            if(!result.empty()) result += "\n";
            result += hex;
        }
        return result;
    }

    static bool firstTag(const char* text, float out[3]){
        static const std::regex tagPattern(
            R"(<\s*Color\s*:\s*R\s*=\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s+G\s*=\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s+B\s*=\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s*>)",
            std::regex_constants::icase);

        std::cmatch match;
        if(!std::regex_search(text ? text : "", match, tagPattern)) return false;
        out[0] = (float)std::stod(match[1].str());
        out[1] = (float)std::stod(match[2].str());
        out[2] = (float)std::stod(match[3].str());
        return true;
    }

    void updateTagFromRgb(){
        const std::string tag = fmtColourTag(rgb[0], rgb[1], rgb[2]);
        std::strncpy(apbTag, tag.c_str(), sizeof(apbTag) - 1);
        apbTag[sizeof(apbTag) - 1] = '\0';
    }

    void draw(){
        ImGui::BeginChild("##hexconv",{0,0},false);
        SectionLabel("Hex Converter");
        SectionNote("Enter RGB values directly or paste an APB colour tag to convert between tag and hex formats.");

        bool rgbEdited = false;
        SectionLabel("RGB Input");
        if(BeginSectionTable("##hexrgb", 80.f, 72.f)){
            BeginSectionRow("Red");
            ImGui::SetNextItemWidth(120.f);
            rgbEdited |= ImGui::InputFloat("##hexr", &rgb[0], 0.f, 0.f, "%.6f");

            BeginSectionRow("Green");
            ImGui::SetNextItemWidth(120.f);
            rgbEdited |= ImGui::InputFloat("##hexg", &rgb[1], 0.f, 0.f, "%.6f");

            BeginSectionRow("Blue");
            ImGui::SetNextItemWidth(120.f);
            rgbEdited |= ImGui::InputFloat("##hexb", &rgb[2], 0.f, 0.f, "%.6f");
            EndSectionTable();
        }
        if(rgbEdited) updateTagFromRgb();

        SectionLabel("APB Tag");
        SectionNote("<Color:R={number} G={number} B={number}>");
        if(BeginSectionTable("##hextaggrid", 80.f, 72.f)){
            BeginSectionRow("Tag");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if(ImGui::InputText("##hextagpreview", apbTag, sizeof(apbTag))){
                float parsed[3];
                if(firstTag(apbTag, parsed)){
                    rgb[0] = parsed[0];
                    rgb[1] = parsed[1];
                    rgb[2] = parsed[2];
                    updateTagFromRgb();
                }
            }
            NextSectionAction();
            if(ImGui::Button("Copy##hextag"))
                ImGui::SetClipboardText(apbTag);
            EndSectionTable();
        }

        output = toHex(rgb[0], rgb[1], rgb[2]);

        SectionLabel("Hex Output");
        if(BeginSectionTable("##hexoutgrid", 80.f, 72.f)){
            BeginSectionRow("Hex");
            ReadOnlyLogBox("##hexout", output, {-1.f, 52.f});
            NextSectionAction();
            if(ImGui::Button("Copy##hexconv"))
                ImGui::SetClipboardText(output.c_str());
            EndSectionTable();
        }
        ImGui::TextColored(Col::SUBTEXT, "Values from 0-1 are normalized RGB; values above 1 are treated as 0-255 RGB.");
        ImGui::EndChild();
    }
};

// ══════════════════════════════════════════════════════════════════════════
// PageLocalization
// ══════════════════════════════════════════════════════════════════════════
struct PageLocalization {
    struct NamedTagRow {
        const char* name;
        bool approximate;
        bool hasSample;
        const char* note;
        float r, g, b, a;
        const char* codeLine1;
        const char* codeLine2;
    };

    static constexpr const char* GENERAL_TEXT = R"(Custom localization configs personalize the game's text on your client only.

Recommended workflow:

1. Use a better editor than basic Notepad. Notepad++ is recommended because folder-wide search matters here.
2. For a first config, copy the full INT folder into GER.
3. Rename the copied files so the extension matches the destination folder.
4. Search the copied GER folder for the exact string you want to change.
5. Edit only the value side of each line:
   StringID=Your text here
6. Keep one string per line. Do not press Enter inside a value.
7. To move text to a new line, use the APB newline character instead of a real line break.
8. Do not break dynamic tags such as <This>. Those are placeholders pulled in by the game.

Useful note:
- APBUserInterface is one of the most common files for UI text, but it is not the only one.
- Missing text after the '=' may fall back to the default English localization.
- To blank out a value safely, use a whitespace character instead of leaving it empty.)";

    static constexpr const char* LAUNCH_TEXT = R"(APB can be forced to load a localization with a shortcut to:
\Binaries\APB.exe

Add the language switch at the end of the shortcut target:
-language=[num]

Example:
"D:\Steam Library\steamapps\common\APB Reloaded\Binaries\APB.exe" -language=1036

Language folders and launch codes:
INT  - International / default English
BRA  - -language=1046
FRA  - -language=1036
GER  - -language=1031
ITA  - -language=1040
RUS  - -language=1049
SPA  - -language=1034

Important:
- INT is the default folder and will be overwritten by game updates.
- If a string is empty in the selected language, APB may fall back to INT.)";

    static constexpr const char* SYMBOLS_TEXT = R"(Known symbols:
~ • × ¡ ¢ £ ¤ ¥ ¦ § ¨ © ª « ¬ ® ¯ ° ± ² ³ ´ µ ¶ · ¸ ¹ º » ¼ ½ ¾
Α Β Χ ∆ Ε Φ Γ Η Ι Κ Λ Μ Ν Ο Π Θ Ρ Σ Τ Υ ς Ω Ξ Ψ Ζ
_ α β χ δ ε φ γ η ι κ λ µ ν ο π θ ρ σ τ υ ω ξ ψ ζ { | }

Special characters:
[↵]  Newline instruction character for in-game line breaks
[ ]  Blank / whitespace character

Notes:
- Do not use the Enter key inside a localization value.
- Some contexts also use the vertical bar '|' as a separator.)";

    static constexpr const char* FONT_NOTES_TEXT = R"(Font rules:

- Font tags can force APB to use a predefined in-game font.
- Fonts are mostly size/style variants, not entirely different typefaces.
- Font changes are known not to work properly on lines that use <col:...> named colour tags.
- Font changes do work with explicit <Color:R= G= B=> tags.
- Do not use <Fonts:/> closing tags. Leave the rest of the line in the chosen font instead.
- EngineFonts use a different character sheet, so some symbols may not render correctly.)";

    static constexpr const char* COLOUR_RULES_TEXT = R"(APB uses two colour-tag systems:

Named tag:
<col:TagName>Text</col>

Explicit RGB tag:
<Color:R=1 G=1 B=1>Text<Color:/>

Termination rules:
- <col:TagName> must be closed with </col>
- <Color:R=... G=... B=...> must be closed with <Color:/>
- Do not swap those closing styles

The game mixes both methods depending on the file and even the individual line.
When changing a string, keep the same tag style APB already used there.

Notes:
- Some tags listed there were measured from current.log
- Entries marked as approximate were matched manually from screen colour picking
- Some tags may map back to plain white or transparent output depending on usage)";

    inline static constexpr NamedTagRow NAMED_TAG_ROWS[] = {
        {"None", false, false, "This color tag actually causes the text to render transparent. No known application of this.", 0.f, 0.f, 0.f, 0.f, "", ""},
        {"Action_Enemy", false, true, nullptr, 0.882353f, 0.f, 0.f, 1.f, "rgba(225,0,0,1)", "R=0.882353 G=0 B=0"},
        {"Action_Enemy_Assist", false, true, nullptr, 0.364706f, 0.f, 0.f, 1.f, "rgba(93,0,0,1)", "R=0.364706 G=0 B=0"},
        {"Action_Team", true, true, nullptr, 0.650980f, 0.992157f, 0.431373f, 1.f, "#A6FD6E", ""},
        {"Action_Team_Assist", true, true, nullptr, 0.349020f, 0.572549f, 0.294118f, 1.f, "#59924B", ""},
        {"Black", false, true, nullptr, 0.f, 0.f, 0.f, 1.f, "rgba(0,0,0,1)", "R=0 G=0 B=0"},
        {"Black_Dark", false, true, nullptr, 0.011765f, 0.011765f, 0.011765f, 1.f, "#030303", "R=0.011765 G=0.011765 B=0.011765"},
        {"Black_Light", false, true, nullptr, 0.019608f, 0.019608f, 0.019608f, 1.f, "#050505", "R=0.019608 G=0.019608 B=0.019608"},
        {"Black_TaskMarker", false, true, nullptr, 0.419608f, 0.419608f, 0.419608f, 1.f, "rgba(107,107,107,1)", "R=0.419608 G=0.419608 B=0.419608"},
        {"Blue", false, true, nullptr, 0.035294f, 0.470588f, 1.f, 1.f, "rgba(9,120,255,1)", "R=0.035294 G=0.470588 B=1"},
        {"Blue_Enforcer", false, true, nullptr, 0.003922f, 0.109804f, 0.239216f, 1.f, "rgba(1,28,61,1)", "R=0.003922 G=0.109804 B=0.239216"},
        {"Blue_Pale", false, true, nullptr, 0.666667f, 0.666667f, 1.f, 1.f, "rgba(170,170,255,1)", "R=0.666667 G=0.666667 B=1"},
        {"Blue_TaskMarker", false, true, nullptr, 0.470588f, 1.f, 1.f, 1.f, "rgba(120,255,255,1)", "R=0.470588 G=1 B=1"},
        {"Blue_mid", false, true, nullptr, 0.196078f, 0.415686f, 1.f, 1.f, "rgba(50,106,255,1)", "R=0.196078 G=0.415686 B=1"},
        {"Bronze", false, true, nullptr, 0.694118f, 0.501961f, 0.101961f, 1.f, "rgba(177,128,26,1)", "R=0.694118 G=0.501961 B=0.101961"},
        {"Ceremony_Highlight", false, true, nullptr, 1.f, 0.882353f, 0.588235f, 1.f, "rgba(255,225,150,1)", "R=1 G=0.882353 B=0.588235"},
        {"Chat_Clan", false, true, nullptr, 0.078431f, 0.274510f, 1.f, 1.f, "rgba(20,70,255,1)", "R=0.078431 G=0.274510 B=1"},
        {"Chat_Combat", false, true, nullptr, 1.f, 0.192157f, 0.019608f, 1.f, "rgba(255,49,5,1)", "R=1 G=0.192157 B=0.019608"},
        {"Chat_Dev", false, true, nullptr, 0.f, 1.f, 1.f, 1.f, "rgba(0,255,255,1)", "R=0 G=1 B=1"},
        {"Chat_District", false, true, nullptr, 0.941176f, 0.941176f, 0.941176f, 1.f, "rgba(240,240,240,1)", "R=0.941176 G=0.941176 B=0.941176"},
        {"Chat_GM", false, true, nullptr, 1.f, 0.f, 0.f, 1.f, "rgba(255,0,0,1)", "R=1 G=0 B=0"},
        {"Chat_Group", false, true, nullptr, 0.250980f, 0.784314f, 0.250980f, 1.f, "rgba(64,200,64,1)", "R=0.250980 G=0.784314 B=0.250980"},
        {"Chat_Helper", false, true, nullptr, 0.f, 1.f, 0.f, 1.f, "rgba(0,255,0,1)", "R=0 G=1 B=0"},
        {"Chat_Mission", false, true, nullptr, 0.929412f, 0.929412f, 0.f, 1.f, "rgba(237,237,0,1)", "R=0.929412 G=0.929412 B=0"},
        {"Chat_Name", false, true, nullptr, 1.f, 0.576471f, 0.015686f, 1.f, "rgba(255,147,4,1)", "R=1 G=0.576471 B=0.015686"},
        {"Chat_Officer", false, true, nullptr, 0.478431f, 0.478431f, 1.f, 1.f, "rgba(122,122,255,1)", "R=0.478431 G=0.478431 B=1"},
        {"Chat_Premium", false, true, nullptr, 0.682353f, 0.568627f, 0.f, 1.f, "rgba(174,145,0,1)", "R=0.682353 G=0.568627 B=0"},
        {"Chat_Say", false, true, nullptr, 1.f, 1.f, 1.f, 1.f, "rgba(255,255,255,1)", "R=1 G=1 B=1"},
        {"Chat_System", false, true, nullptr, 0.682353f, 0.682353f, 0.682353f, 1.f, "rgba(174,174,174,1)", "R=0.682353 G=0.682353 B=0.682353"},
        {"Chat_TGM", false, true, nullptr, 1.f, 0.337255f, 0.337255f, 1.f, "rgba(255,86,86,1)", "R=1 G=0.337255 B=0.337255"},
        {"Chat_Team", false, true, nullptr, 0.188235f, 1.f, 1.f, 1.f, "rgba(48,255,255,1)", "R=0.188235 G=1 B=1"},
        {"Chat_Tutorial", false, true, nullptr, 0.f, 0.490196f, 0.650980f, 1.f, "rgba(0,125,166,1)", "R=0 G=0.490196 B=0.650980"},
        {"Chat_Vehicle", false, true, nullptr, 1.f, 0.192157f, 0.019608f, 1.f, "rgba(255,49,5,1)", "R=1 G=0.192157 B=0.019608"},
        {"Chat_Whisper", false, true, nullptr, 0.933333f, 0.f, 0.933333f, 1.f, "rgba(238,0,238,1)", "R=0.933333 G=0 B=0.933333"},
        {"Chat_Yell", false, true, nullptr, 1.f, 0.419608f, 0.317647f, 1.f, "rgba(255,107,81,1)", "R=1 G=0.419608 B=0.317647"},
        {"Contact", false, true, nullptr, 0.400000f, 0.674510f, 0.007843f, 1.f, "rgba(102,172,2,1)", "R=0.400000 G=0.674510 B=0.007843"},
        {"Cyan", false, true, nullptr, 0.011765f, 0.549020f, 0.784314f, 1.f, "rgba(3,140,200,1)", "R=0.011765 G=0.549020 B=0.784314"},
        {"ED_garage", false, true, nullptr, 0.650980f, 0.f, 0.f, 1.f, "rgba(166,0,0,1)", "R=0.650980 G=0 B=0"},
        {"ED_marketplace", false, true, nullptr, 0.f, 0.650980f, 0.f, 1.f, "rgba(0,166,0,1)", "R=0 G=0.650980 B=0"},
        {"ED_music", false, true, nullptr, 0.650980f, 0.650980f, 0.650980f, 1.f, "rgba(166,166,166,1)", "R=0.650980 G=0.650980 B=0.650980"},
        {"ED_persona", false, true, nullptr, 0.650980f, 0.325490f, 0.f, 1.f, "rgba(166,83,0,1)", "R=0.650980 G=0.325490 B=0"},
        {"ED_symbol", false, true, nullptr, 0.f, 0.862745f, 0.831373f, 1.f, "rgba(0,220,212,1)", "R=0 G=0.862745 B=0.831373"},
        {"ED_wardrobe", false, true, nullptr, 0.650980f, 0.f, 0.650980f, 1.f, "rgba(166,0,166,1)", "R=0.650980 G=0 B=0.650980"},
        {"Green", false, true, nullptr, 0.078431f, 1.f, 0.f, 1.f, "rgba(20,255,0,1)", "R=0.078431 G=1 B=0"},
        {"Green_Dark", false, true, nullptr, 0.098039f, 0.549020f, 0.196078f, 1.f, "rgba(25,140,50,1)", "R=0.098039 G=0.549020 B=0.196078"},
        {"Green_Light", false, true, nullptr, 0.011765f, 1.f, 0.541176f, 1.f, "rgba(3,255,138,1)", "R=0.011765 G=1 B=0.541176"},
        {"Green_Pale", false, true, nullptr, 0.400000f, 1.f, 0.f, 1.f, "rgba(102,255,0,1)", "R=0.400000 G=1 B=0"},
        {"Green_TaskMarker", false, true, nullptr, 0.270588f, 1.f, 0.086275f, 1.f, "rgba(69,255,22,1)", "R=0.270588 G=1 B=0.086275"},
        {"Grey", false, true, nullptr, 0.349020f, 0.349020f, 0.349020f, 1.f, "rgba(89,89,89,1)", "R=0.349020 G=0.349020 B=0.349020"},
        {"Grey_Dark", false, true, nullptr, 0.035294f, 0.035294f, 0.035294f, 1.f, "rgba(9,9,9,1)", "R=0.035294 G=0.035294 B=0.035294"},
        {"Grey_Pale", false, true, nullptr, 0.882353f, 0.882353f, 0.882353f, 1.f, "rgba(225,225,225,1)", "R=0.882353 G=0.882353 B=0.882353"},
        {"HUDMessage_Default", true, true, nullptr, 0.984314f, 0.682353f, 0.129412f, 1.f, "#FBAE21", ""},
        {"HUDMessage_Error", false, true, nullptr, 0.776471f, 0.f, 0.f, 1.f, "rgba(198,0,0,1)", "R=0.776471 G=0 B=0"},
        {"HUDMessage_Mission", false, true, nullptr, 0.705882f, 0.627451f, 0.f, 1.f, "rgba(180,160,0,1)", "R=0.705882 G=0.627451 B=0"},
        {"HUDMessage_TutorialText", false, true, nullptr, 0.776471f, 0.843137f, 0.870588f, 1.f, "rgba(198,215,222,1)", "R=0.776471 G=0.843137 B=0.870588"},
        {"HUDMessage_VIP", false, true, nullptr, 0.188235f, 1.f, 1.f, 1.f, "rgba(48,255,255,1)", "R=0.188235 G=1 B=1"},
        {"Mailbox", false, true, nullptr, 0.f, 0.419608f, 1.f, 1.f, "rgba(0,107,255,1)", "R=0 G=0.419608 B=1"},
        {"Orange", false, true, nullptr, 1.f, 0.400000f, 0.f, 1.f, "rgba(255,102,0,1)", "R=1 G=0.400000 B=0"},
        {"Orange_APB", true, true, nullptr, 0.917647f, 0.666667f, 0.098039f, 1.f, "#EAAA19", ""},
        {"Orange_Dark", false, true, nullptr, 0.588235f, 0.117647f, 0.f, 1.f, "rgba(150,30,0,1)", "R=0.588235 G=0.117647 B=0"},
        {"Pink", false, true, nullptr, 0.996078f, 0.164706f, 0.400000f, 1.f, "rgba(254,42,102,1)", "R=0.996078 G=0.164706 B=0.400000"},
        {"Pink_Pale", false, true, nullptr, 0.882353f, 0.576471f, 0.662745f, 1.f, "#E193A9", "R=0.882353 G=0.576471 B=0.662745"},
        {"Purple", false, true, nullptr, 0.596078f, 0.082353f, 0.768627f, 1.f, "rgba(152,21,196,1)", "R=0.596078 G=0.082353 B=0.768627"},
        {"Purple_Bright", false, true, nullptr, 0.819608f, 0.082353f, 0.768627f, 1.f, "rgba(209,21,196,1)", "R=0.819608 G=0.082353 B=0.768627"},
        {"Purple_Dark", false, true, nullptr, 0.211765f, 0.027451f, 1.f, 1.f, "rgba(54,7,255,1)", "R=0.211765 G=0.027451 B=1"},
        {"Red", false, true, nullptr, 1.f, 0.f, 0.f, 1.f, "rgba(255,0,0,1)", "R=1 G=0 B=0"},
        {"Red_Criminal", false, true, nullptr, 0.239216f, 0.003922f, 0.003922f, 1.f, "rgba(61,1,1,1)", "R=0.239216 G=0.003922 B=0.003922"},
        {"Red_Dark", false, true, nullptr, 0.725490f, 0.f, 0.f, 1.f, "rgba(185,0,0,1)", "R=0.725490 G=0 B=0"},
        {"Red_Pale", false, true, nullptr, 0.313725f, 0.f, 0.f, 1.f, "rgba(80,0,0,1)", "R=0.313725 G=0 B=0"},
        {"Red_mid", false, true, nullptr, 1.f, 0.215686f, 0.117647f, 1.f, "rgba(255,55,30,1)", "R=1 G=0.215686 B=0.117647"},
        {"ScoreBreakdown_Cash", false, true, nullptr, 0.984314f, 0.721569f, 0.015686f, 1.f, "rgba(251,184,4,1)", "R=0.984314 G=0.721569 B=0.015686"},
        {"ScoreBreakdown_NormalValue", false, true, nullptr, 0.007843f, 0.560784f, 0.866667f, 1.f, "rgba(2,143,221,1)", "R=0.007843 G=0.560784 B=0.866667"},
        {"Scoreboard_LocalPlayer", false, true, nullptr, 1.f, 0.800000f, 0.f, 1.f, "rgba(255,204,0,1)", "R=1 G=0.800000 B=0"},
        {"Scoreboard_Opponents", false, true, nullptr, 0.776471f, 0.f, 0.f, 1.f, "rgba(198,0,0,1)", "R=0.776471 G=0 B=0"},
        {"Tutorial_KeyPress", false, true, nullptr, 1.f, 0.400000f, 0.f, 1.f, "rgba(255,102,0,1)", "R=1 G=0.400000 B=0"},
        {"Valentine_Pink", false, true, nullptr, 1.f, 0.517647f, 0.862745f, 1.f, "rgba(255,132,220,1)", "R=1 G=0.517647 B=0.862745"},
        {"White", false, true, nullptr, 1.f, 1.f, 1.f, 1.f, "rgba(255,255,255,1)", "R=1 G=1 B=1"},
        {"Yellow", false, true, nullptr, 1.f, 0.674510f, 0.f, 1.f, "rgba(255,172,0,1)", "R=1 G=0.674510 B=0"},
        {"Yellow_Bright", false, true, nullptr, 1.f, 0.388235f, 0.058824f, 1.f, "rgba(255,99,15,1)", "R=1 G=0.388235 B=0.058824"},
        {"Yellow_CSA", false, true, nullptr, 1.f, 0.098039f, 0.f, 1.f, "rgba(255,25,0,1)", "R=1 G=0.098039 B=0"},
        {"Yellow_Pale", false, true, nullptr, 1.f, 1.f, 0.725490f, 1.f, "rgba(255,255,185,1)", "R=1 G=1 B=0.725490"},
        {"Yellow_TaskMarker", false, true, nullptr, 1.f, 0.760784f, 0.047059f, 0.882353f, "rgba(255,194,12,225)", "R=1 G=0.760784 B=0.047059"},
    };

    static ImU32 tagColor(const NamedTagRow& row){
        return IM_COL32(
            std::clamp((int)std::lround(row.r * 255.f), 0, 255),
            std::clamp((int)std::lround(row.g * 255.f), 0, 255),
            std::clamp((int)std::lround(row.b * 255.f), 0, 255),
            std::clamp((int)std::lround(row.a * 255.f), 0, 255));
    }

    static void drawTagSample(const NamedTagRow& row){
        if(!row.hasSample){
            ImGui::PushStyleColor(ImGuiCol_Text, Col::SUBTEXT);
            ImGui::TextWrapped("%s", row.note ? row.note : "");
            ImGui::PopStyleColor();
            return;
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float blockW = 15.f;
        const float blockH = 18.f;
        const float gap = 6.f;
        const ImU32 col = tagColor(row);
        dl->AddRectFilled(start, {start.x + blockW * 4.f, start.y + blockH}, col);

        ImGui::Dummy({blockW * 4.f, blockH});
        ImGui::SameLine(0.f, gap);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col),
            "The quick brown fox jumps over the lazy dog.");
    }

    static void drawTagCode(const NamedTagRow& row){
        if(row.codeLine1 && row.codeLine1[0])
            ImGui::TextUnformatted(row.codeLine1);
        if(row.codeLine2 && row.codeLine2[0]){
            const std::string code = row.codeLine2;
            const size_t rPos = code.find("R=");
            const size_t gPos = code.find(" G=");
            const size_t bPos = code.find(" B=");
            const std::string rVal = (rPos != std::string::npos && gPos != std::string::npos)
                ? code.substr(rPos + 2, gPos - (rPos + 2)) : "";
            const std::string gVal = (gPos != std::string::npos && bPos != std::string::npos)
                ? code.substr(gPos + 3, bPos - (gPos + 3)) : "";
            const std::string bVal = (bPos != std::string::npos)
                ? code.substr(bPos + 3) : "";

            ImGui::TextColored(ImVec4(1.f, 0.2f, 0.2f, 1.f), "R=");
            ImGui::SameLine(0.f, 0.f);
            ImGui::TextUnformatted(rVal.c_str());
            ImGui::SameLine(0.f, 0.f);
            ImGui::TextColored(ImVec4(0.2f, 0.85f, 0.2f, 1.f), " G=");
            ImGui::SameLine(0.f, 0.f);
            ImGui::TextUnformatted(gVal.c_str());
            ImGui::SameLine(0.f, 0.f);
            ImGui::TextColored(ImVec4(0.2f, 0.4f, 1.f, 1.f), " B=");
            ImGui::SameLine(0.f, 0.f);
            ImGui::TextUnformatted(bVal.c_str());
        }
    }

    static void drawNamedTagsTable(){
        if(!ImGui::BeginTable("##locnamedtagstable", 4,
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingStretchProp,
            {-1.f, -1.f}))
            return;

        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 210.f);
        ImGui::TableSetupColumn("~", ImGuiTableColumnFlags_WidthFixed, 36.f);
        ImGui::TableSetupColumn("Sample/Notes", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Code", ImGuiTableColumnFlags_WidthFixed, 270.f);
        ImGui::TableHeadersRow();

        for(const auto& row : NAMED_TAG_ROWS){
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(row.name);

            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            if(row.approximate)
                ImGui::TextColored(Col::SUBTEXT, "~");

            ImGui::TableSetColumnIndex(2);
            drawTagSample(row);

            ImGui::TableSetColumnIndex(3);
            drawTagCode(row);
        }

        ImGui::EndTable();
    }

    void draw(){
        static const char* fonts[] = {
            "<Fonts:APBMenus_Font.APB_Helvetica_Bold_11>",
            "<Fonts:APBMenus_Font.APB_Helvetica_Bold_13>",
            "<Fonts:APBMenus_Font.APB_Helvetica_Bold_14>",
            "<Fonts:APBMenus_Font.APB_Helvetica_Bold_24>",
            "<Fonts:APBMenus_Font.APB_Helvetica_Bold_32>",
            "<Fonts:APBMenus_Font.APB_Helvetica_Regular_11>",
            "<Fonts:APBMenus_Font.APB_Helvetica_Regular_12>",
            "<Fonts:APBMenus_Font.APB_Helvetica_Regular_14>",
            "<Fonts:APBMenus_Font.APB_Helvetica_Regular_16>",
            "<Fonts:APBMenus_Font.APB_Helvetica_Regular_28>",
            "<Fonts:APBMenus_Font.APB_HUD_AmmoCounter>",
            "<Fonts:EngineFonts.TinyFont>",
            "<Fonts:EngineFonts.SmallFont>",
            "<Fonts:EngineFonts.MediumFont>",
            "<Fonts:EngineFonts.LargeFont>",
        };

        ImGui::BeginChild("##loc",{0,0},false);
        SectionLabel("Localization Reference");
        SectionNote("Setup, launch arguments, symbols, fonts, and colour-tag behaviour are grouped here for quick editing reference.");
        if(ImGui::BeginTabBar("##loctabs")){
            if(ImGui::BeginTabItem("General")){
                SubLabel("Workflow");
                ReadOnlyLogBox("##locgeneral", GENERAL_TEXT, {-1.f, -1.f});
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Launch")){
                SubLabel("Language Override");
                ReadOnlyLogBox("##loclaunch", LAUNCH_TEXT, {-1.f, -1.f});
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Symbols")){
                SubLabel("Special Characters");
                if(ImFont* refFont = ResolveLocalizationReferenceFont())
                    ImGui::PushFont(refFont);
                ReadOnlyLogBox("##locsymbols", SYMBOLS_TEXT, {-1.f, -1.f});
                if(ResolveLocalizationReferenceFont())
                    ImGui::PopFont();
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Fonts")){
                SubLabel("Known Font Tags");
                ReadOnlyLogBox("##locfontnotes", FONT_NOTES_TEXT, {-1.f, 130.f});
                ImGui::Spacing();
                for(auto f:fonts){ if(ImGui::Selectable(f)) ImGui::SetClipboardText(f); }
                ImGui::TextColored(Col::SUBTEXT,"Click any font tag to copy it.");
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Tag Rules")){
                SubLabel("Colour Syntax");
                ReadOnlyLogBox("##loccolourrules", COLOUR_RULES_TEXT, {-1.f, -1.f});
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Named Tags")){
                SubLabel("Measured Colour Tags");
                drawNamedTagsTable();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
    }
};

// ══════════════════════════════════════════════════════════════════════════
// PageCredits
// ══════════════════════════════════════════════════════════════════════════
struct PageCredits {
    void draw(){
        ImGui::BeginChild("##cred",{0,0},false);
        SectionLabel("Credits");
        SectionLabel("About");
        ImGui::PushStyleColor(ImGuiCol_Text,Col::YELLOW);
        ImGui::TextUnformatted("APB Config Tool");
        ImGui::PopStyleColor();
        ImGui::TextWrapped("An open-source localisation and configuration utility for All Points Bulletin: Reloaded.");
        SectionLabel("Contributors");
        ImGui::Text("Author");        ImGui::SameLine(120); ImGui::TextUnformatted("writch");
        ImGui::Text("Credits");       ImGui::SameLine(120); ImGui::TextWrapped("Mewpri - original creator of the ItemTypes stat maker");
        SectionLabel("Legal");
        ImGui::TextColored(Col::SUBTEXT,
            "This tool is not affiliated with Little Orbit or GamersFirst.\n"
            "APB: Reloaded(tm) is a trademark of Little Orbit LLC.");
        ImGui::EndChild();
    }
};

// ══════════════════════════════════════════════════════════════════════════
// PageSettings
// ══════════════════════════════════════════════════════════════════════════
struct PageSettings {
    char premadeConfigDir[MAX_PATH * 2] = {};
    bool premadeConfigDirSeeded = false;

    void syncPremadeConfigDir(){
        if(premadeConfigDirSeeded) return;
        std::snprintf(premadeConfigDir, sizeof(premadeConfigDir), "%s", premadeConfigLocation().c_str());
        premadeConfigDirSeeded = true;
    }

    void applyPremadeConfigDir(){
        setPremadeConfigLocation(premadeConfigDir);
        refreshPremadeConfigSummaries();
        std::snprintf(premadeConfigDir, sizeof(premadeConfigDir), "%s", premadeConfigLocation().c_str());
    }

    void draw(){
        syncPremadeConfigDir();

        ImGui::BeginChild("##settings",{0,0},false);
        SectionLabel("Settings");
        SectionNote("Shared application resources live here so the generator pages can stay focused on output configuration.");

        SectionLabel("Themes");
        ImGui::TextWrapped("Theme presets are loaded from this folder. Add or edit JSON files here, then reload themes from the tool pages.");
        ImGui::Spacing();

        const std::string themesDir = ThemeLibrary::themesDir();
        std::filesystem::create_directories(themesDir);

        char themePathBuf[MAX_PATH * 2] = {};
        std::snprintf(themePathBuf, sizeof(themePathBuf), "%s", themesDir.c_str());

        ImGui::Text("Folder:");
        ImGui::SetNextItemWidth(-150.f);
        ImGui::InputText("##themesFolderPath", themePathBuf, sizeof(themePathBuf), ImGuiInputTextFlags_ReadOnly);
        ImGui::SameLine();
        if(ImGui::Button("Open Themes Folder", {140.f, 0.f}))
            OpenInExplorer(themesDir);

        SectionLabel("Premade Configs");
        SectionNote("Templates are scanned from this folder at runtime. Each immediate subfolder is treated as one premade config.");

        const std::string defaultPremadeDir = PremadeConfigsDir();
        std::filesystem::create_directories(defaultPremadeDir);

        ImGui::Text("Folder:");
        ImGui::SetNextItemWidth(-220.f);
        if(ImGui::InputText("##premadeConfigLocation", premadeConfigDir, sizeof(premadeConfigDir),
            ImGuiInputTextFlags_AutoSelectAll) && !ImGui::IsItemActive()){
            applyPremadeConfigDir();
        }
        if(ImGui::IsItemDeactivatedAfterEdit())
            applyPremadeConfigDir();
        ImGui::SameLine();
        if(ImGui::Button("Browse##premadeConfigLocation", {70.f, 0.f})){
            std::string picked;
            if(BrowseFolder(picked, "Select premade config folder")){
                std::snprintf(premadeConfigDir, sizeof(premadeConfigDir), "%s", picked.c_str());
                applyPremadeConfigDir();
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("Open##premadeConfigLocation", {60.f, 0.f}))
            OpenInExplorer(premadeConfigLocation());

        ImGui::Spacing();
        if(ImGui::Button("Use Default Premade Folder", {180.f, 0.f})){
            std::snprintf(premadeConfigDir, sizeof(premadeConfigDir), "%s", defaultPremadeDir.c_str());
            applyPremadeConfigDir();
        }
        ImGui::SameLine();
        if(ImGui::Button("Refresh Premade Scan", {150.f, 0.f}))
            applyPremadeConfigDir();

        const auto summaries = premadeConfigSummaries();
        int totalFiles = 0;
        for(const auto& summary : summaries)
            totalFiles += summary.fileCount;
        ImGui::TextColored(Col::SUBTEXT, "Resolved folder: %s", premadeConfigLocation().c_str());
        if(isPremadeCacheBuilding()){
            int done, total;
            premadeCacheBuildProgress(done, total);
            const float frac = (total > 0) ? (float)done / (float)total : 0.f;
            ImGui::ProgressBar(frac, {-1.f, 0.f}, "Scanning...");
        } else {
            ImGui::TextColored(Col::SUBTEXT, "Detected %d templates across %d files.", (int)summaries.size(), totalFiles);
        }

        ImGui::EndChild();
    }
};

// ══════════════════════════════════════════════════════════════════════════
// PagePremadeConfigs
// ══════════════════════════════════════════════════════════════════════════
struct PagePremadeConfigs {
    int templateIdx = 0;
    char outputDir[MAX_PATH] = {};
    std::atomic<bool> running{false};
    std::atomic<bool> cancelRequested{false};
    std::string lastOutputDir;
    bool outputSeeded = false;

    int lastEditableTemplateIdx = -1;
    std::map<std::string, std::string>         namedOverrides;
    std::map<std::string, std::array<float,3>> rgbOverrides;

    struct GradientOverrideState {
        bool enabled = false;
        bool seeded  = false;
        GradientType outputType = GradientType::Smooth;
        std::string origStartValue;
        std::string origMidValue;
        std::string origEndValue;
        std::array<float,3> startRgb = {1.f,1.f,1.f};
        std::array<float,3> midRgb   = {0.5f,0.5f,0.5f};
        std::array<float,3> endRgb   = {1.f,1.f,1.f};
        // Stepped palette: original normalised values + user-chosen replacement colours
        std::vector<std::string>          origPalette;
        std::vector<std::array<float,3>>  paletteRgb;
    };
    std::map<std::string, GradientOverrideState> gradientOverrides;

    bool isActionRunning() const { return running.load(); }

    bool canStartAction() const {
        const auto& summaries = premadeConfigSummaries();
        return !running.load()
            && templateIdx >= 0
            && templateIdx < (int)summaries.size()
            && outputDir[0] != '\0';
    }

    void cancelAction(){
        if(!running.load()) return;
        cancelRequested = true;
    }

    void seedDefaultOutput(){
        if(outputSeeded && outputDir[0] != '\0') return;
        const std::string preferred = DownloadsDir() + "\\APB Premade Localization";
        std::snprintf(outputDir, sizeof(outputDir), "%s", preferred.c_str());
        outputSeeded = true;
    }

    std::string resolvedOutputPreview() const {
        if(!outputDir[0]) return {};
        std::filesystem::path out(outputDir);
        out /= "APBCT_YYYYMMDD_HHMMSS";
        return out.string();
    }

    void startAction(){
        if(!canStartAction()) return;

        const auto summaries = premadeConfigSummaries();

        PremadeConfigExportOptions options;
        options.templateName = summaries[(size_t)templateIdx].name;
        options.outputDir    = outputDir;

        // Build colour substitutions from enabled gradient overrides
        const auto& summary = summaries[(size_t)templateIdx];
        for(const auto& grad : summary.editableGradients){
            if(grad.sequence.empty()) continue;
            auto it = gradientOverrides.find(grad.sequence);
            if(it == gradientOverrides.end() || !it->second.enabled) continue;
            const GradientOverrideState& ov = it->second;

            // Split sequence into individual step values
            std::vector<std::string> steps;
            {
                std::istringstream ss(grad.sequence);
                std::string s;
                while(std::getline(ss, s, '|'))
                    if(!s.empty()) steps.push_back(s);
            }
            const int n = (int)steps.size();
            if(n < 1) continue;

            if(ov.outputType == GradientType::Stepped){
                // Direct palette substitution: origPalette[i] → paletteRgb[i]
                const int nu = (int)std::min(ov.origPalette.size(), ov.paletteRgb.size());
                for(int ui = 0; ui < nu; ++ui){
                    const auto& col = ov.paletteRgb[(size_t)ui];
                    options.colourSubstitutions[ov.origPalette[(size_t)ui]] =
                        "R=" + fmtF(col[0]) + " G=" + fmtF(col[1]) + " B=" + fmtF(col[2]);
                }
            } else {
                for(int i = 0; i < n; ++i){
                    const float t = (n > 1) ? (float)i / (float)(n - 1) : 0.f;
                    float nr, ng, nb;
                    if(ov.outputType == GradientType::Triple){
                        const float t2 = (t <= 0.5f) ? t * 2.f : (t - 0.5f) * 2.f;
                        const auto& base = (t <= 0.5f) ? ov.startRgb : ov.midRgb;
                        const auto& dest = (t <= 0.5f) ? ov.midRgb   : ov.endRgb;
                        nr = base[0] + (dest[0] - base[0]) * t2;
                        ng = base[1] + (dest[1] - base[1]) * t2;
                        nb = base[2] + (dest[2] - base[2]) * t2;
                    } else {
                        nr = ov.startRgb[0] + (ov.endRgb[0] - ov.startRgb[0]) * t;
                        ng = ov.startRgb[1] + (ov.endRgb[1] - ov.startRgb[1]) * t;
                        nb = ov.startRgb[2] + (ov.endRgb[2] - ov.startRgb[2]) * t;
                    }
                    options.colourSubstitutions[steps[(size_t)i]] =
                        "R=" + fmtF(nr) + " G=" + fmtF(ng) + " B=" + fmtF(nb);
                }
            }
        }

        cancelRequested = false;
        running = true;

        std::thread([this, options](){
            try{
                PremadeConfigExportResult result;
                exportPremadeConfig(options, result, nullptr, &cancelRequested);
                if(!result.cancelled)
                    lastOutputDir = result.outputDir;
            }catch(...){}
            running = false;
        }).detach();
    }

    static ImVec4 namedTagColor(const std::string& name){
        for(const auto& row : PageLocalization::NAMED_TAG_ROWS)
            if(name == row.name)
                return {row.r, row.g, row.b, std::max(row.a, 0.15f)};
        return {0.5f, 0.5f, 0.5f, 1.f};
    }

    static bool parseRgbValue(const std::string& value, float rgb[3]){
        float r = 0.5f, g = 0.5f, b = 0.5f;
        if(std::sscanf(value.c_str(), "R=%f G=%f B=%f", &r, &g, &b) == 3){
            rgb[0] = std::clamp(r, 0.f, 1.f);
            rgb[1] = std::clamp(g, 0.f, 1.f);
            rgb[2] = std::clamp(b, 0.f, 1.f);
            return true;
        }
        rgb[0] = 0.5f; rgb[1] = 0.5f; rgb[2] = 0.5f;
        return false;
    }

    static const char* namedTagDescription(const std::string& name){
        if(name == "None")                        return "Renders text transparent / invisible";
        // Action / mission
        if(name == "Action_Enemy")                return "Enemy player indicators during missions";
        if(name == "Action_Enemy_Assist")         return "Enemy assist player indicators during missions";
        if(name == "Action_Team")                 return "Team / ally indicators during missions";
        if(name == "Action_Team_Assist")          return "Team assist indicators during missions";
        // Faction
        if(name == "Blue_Enforcer")               return "Enforcer faction";
        if(name == "Red_Criminal")                return "Criminal faction";
        // Task markers
        if(name == "Black_TaskMarker")            return "Task / objective marker";
        if(name == "Blue_TaskMarker")             return "Task / objective marker";
        if(name == "Green_TaskMarker")            return "Task / objective marker";
        if(name == "Yellow_TaskMarker")           return "Task / objective marker";
        // Chat channels
        if(name == "Chat_Clan")                   return "Clan chat channel";
        if(name == "Chat_Combat")                 return "Combat event messages";
        if(name == "Chat_Dev")                    return "Developer / staff announcements";
        if(name == "Chat_District")               return "District-wide chat";
        if(name == "Chat_GM")                     return "Game Master messages";
        if(name == "Chat_Group")                  return "Group / squad chat";
        if(name == "Chat_Helper")                 return "Helper role chat";
        if(name == "Chat_Mission")                return "Mission-related chat messages";
        if(name == "Chat_Name")                   return "Player name shown in chat";
        if(name == "Chat_Officer")                return "Clan officer chat";
        if(name == "Chat_Premium")                return "Premium / Armas subscriber chat";
        if(name == "Chat_Say")                    return "Local / say chat";
        if(name == "Chat_System")                 return "System / server messages";
        if(name == "Chat_TGM")                    return "Trial Game Master messages";
        if(name == "Chat_Team")                   return "Team chat channel";
        if(name == "Chat_Tutorial")               return "Tutorial guidance messages";
        if(name == "Chat_Vehicle")                return "Vehicle-related chat messages";
        if(name == "Chat_Whisper")                return "Private / whisper messages";
        if(name == "Chat_Yell")                   return "Yell / zone-wide chat";
        // Contacts / NPC
        if(name == "Contact")                     return "Contact NPC names";
        // Electronic District icons
        if(name == "ED_garage")                   return "Electronic District \xe2\x80\x93 Garage";
        if(name == "ED_marketplace")              return "Electronic District \xe2\x80\x93 Marketplace";
        if(name == "ED_music")                    return "Electronic District \xe2\x80\x93 Music";
        if(name == "ED_persona")                  return "Electronic District \xe2\x80\x93 Persona / character";
        if(name == "ED_symbol")                   return "Electronic District \xe2\x80\x93 Symbol editor";
        if(name == "ED_wardrobe")                 return "Electronic District \xe2\x80\x93 Wardrobe / clothing";
        // HUD messages
        if(name == "HUDMessage_Default")          return "Default HUD notification messages";
        if(name == "HUDMessage_Error")            return "Error / failure HUD notifications";
        if(name == "HUDMessage_Mission")          return "Mission update HUD notifications";
        if(name == "HUDMessage_TutorialText")     return "Tutorial HUD text";
        if(name == "HUDMessage_VIP")              return "VIP / priority HUD notifications";
        // Misc UI
        if(name == "Mailbox")                     return "Mailbox / inbox indicator";
        if(name == "Ceremony_Highlight")          return "Ceremony and event highlight text";
        if(name == "Bronze")                      return "Bronze / third-place ranking";
        if(name == "Orange_APB")                  return "APB brand orange";
        if(name == "ScoreBreakdown_Cash")         return "Cash / money values in score breakdown";
        if(name == "ScoreBreakdown_NormalValue")  return "Standard values in score breakdown";
        if(name == "ScoreBreakdown_NegativeValue")return "Negative / loss values in score breakdown";
        if(name == "Scoreboard_LocalPlayer")      return "Local player row on the scoreboard";
        if(name == "Scoreboard_Opponents")        return "Opponent rows on the scoreboard";
        if(name == "Tutorial_KeyPress")           return "Key press prompts in tutorials";
        if(name == "Valentine_Pink")              return "Valentine's Day / seasonal event";
        if(name == "Yellow_CSA")                  return "CSA (Civilian Support Agency) colour";
        if(name == "GroupHUD_OutOfDistrict")      return "Group HUD \xe2\x80\x93 member out of district";
        if(name == "openworld_marker")            return "Open world map marker";
        if(name == "Halloween_Necrocite")         return "Halloween event / Necrocite";
        if(name == "StageText")                   return "Mission stage text overlay";
        return nullptr;
    }

    static void drawColourTagTooltip(const PremadeConfigEditableValue& value){
        ImGui::BeginTooltip();
        if(value.kind == "Named"){
            const PageLocalization::NamedTagRow* found = nullptr;
            for(const auto& row : PageLocalization::NAMED_TAG_ROWS)
                if(value.value == row.name){ found = &row; break; }

            // Header: swatch + tag name
            ImVec4 swatchCol = found
                ? ImVec4{found->r, found->g, found->b, std::max(found->a, 0.15f)}
                : ImVec4{0.5f, 0.5f, 0.5f, 1.f};
            ImGui::ColorButton("##ttip_sw", swatchCol,
                ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                {48.f, 24.f});
            ImGui::SameLine();
            ImGui::TextUnformatted(value.value.c_str());

            // Samples: the actual text this colour is applied to
            if(!value.samples.empty()){
                ImGui::Separator();
                for(const auto& s : value.samples)
                    ImGui::BulletText("%s", s.c_str());
            }

            // Reference codes
            if(found){
                if((found->codeLine1 && found->codeLine1[0]) || (found->codeLine2 && found->codeLine2[0])){
                    ImGui::Separator();
                    if(found->codeLine1 && found->codeLine1[0])
                        ImGui::TextColored(Col::SUBTEXT, "%s", found->codeLine1);
                    if(found->codeLine2 && found->codeLine2[0]){
                        char buf[128];
                        std::snprintf(buf, sizeof(buf), "<Color:%s>", found->codeLine2);
                        ImGui::TextColored(Col::SUBTEXT, "%s", buf);
                    }
                }
            }
        } else {
            float rgb[3];
            parseRgbValue(value.value, rgb);
            const int ri = std::clamp((int)(rgb[0] * 255.f + 0.5f), 0, 255);
            const int gi = std::clamp((int)(rgb[1] * 255.f + 0.5f), 0, 255);
            const int bi = std::clamp((int)(rgb[2] * 255.f + 0.5f), 0, 255);
            char hex[16];
            std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", ri, gi, bi);

            ImGui::ColorButton("##ttip_sw", {rgb[0], rgb[1], rgb[2], 1.f},
                ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                {48.f, 24.f});
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextUnformatted(value.value.c_str());
            ImGui::TextColored(Col::SUBTEXT, "%s", hex);
            ImGui::EndGroup();

            if(!value.samples.empty()){
                ImGui::Separator();
                for(const auto& s : value.samples)
                    ImGui::BulletText("%s", s.c_str());
            }
        }
        ImGui::EndTooltip();
    }

    void draw(){
        seedDefaultOutput();

        if(templateIdx != lastEditableTemplateIdx){
            lastEditableTemplateIdx = templateIdx;
            namedOverrides.clear();
            rgbOverrides.clear();
            gradientOverrides.clear();
        }

        const auto& summaries = premadeConfigSummaries();
        if(!summaries.empty())
            templateIdx = std::clamp(templateIdx, 0, (int)summaries.size() - 1);
        else
            templateIdx = 0;

        ImGui::BeginChild("##premadeconfigs", {0, 0}, false);
        SectionLabel("Premade Configs");
        SectionNote("Place a full-config folder in the configured premade folder, select it here, review its colour tags, then export with OK.");

        SectionLabel("Template");
        if(BeginSectionTable("##premadetemplate", 124.f, 110.f)){
            BeginSectionRow("Config");
            ImGui::SetNextItemWidth(-FLT_MIN);
            const char* preview = summaries.empty() ? "No scanned templates" : summaries[(size_t)templateIdx].name.c_str();
            if(ImGui::BeginCombo("##premadecombo", preview)){
                for(int i = 0; i < (int)summaries.size(); ++i){
                    const bool selected = templateIdx == i;
                    if(ImGui::Selectable(summaries[(size_t)i].name.c_str(), selected))
                        templateIdx = i;
                    if(selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            BeginSectionRow("Summary");
            if(isPremadeCacheBuilding()){
                int done, total;
                premadeCacheBuildProgress(done, total);
                const float frac = (total > 0) ? (float)done / (float)total : 0.f;
                char overlay[32];
                if(total > 0)
                    std::snprintf(overlay, sizeof(overlay), "%d / %d files", done, total);
                else
                    std::snprintf(overlay, sizeof(overlay), "Scanning...");
                ImGui::ProgressBar(frac, {-1.f, 0.f}, overlay);
            } else if(summaries.empty()){
                ImGui::TextColored(Col::RED, "No premade configs found in the configured premade folder.");
            } else {
                const auto& summary = summaries[(size_t)templateIdx];
                ImGui::Text("%d files", summary.fileCount);
                ImGui::SameLine();
                ImGui::TextColored(Col::SUBTEXT, "| %d tagged | %d named | %d RGB | %d gradients",
                    summary.recolourableFiles, summary.namedColourTagCount,
                    summary.rgbColourTagCount, summary.gradientRunCount);
            }
            EndSectionTable();
        }

        if(!summaries.empty()){
            const auto& summary = summaries[(size_t)templateIdx];

            if(!summary.editableValues.empty() || !summary.editableGradients.empty()){
                SectionLabel("Colour Tags");

                if(!summary.editableValues.empty()){
                    const float rowH  = ImGui::GetFrameHeightWithSpacing();
                    const int   shown = std::min((int)summary.editableValues.size(), 8);
                    if(ImGui::BeginTable("##premadevals", 4,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                        {0.f, shown * rowH + 6.f})){
                        ImGui::TableSetupScrollFreeze(0, 1);
                        ImGui::TableSetupColumn("##sw",  ImGuiTableColumnFlags_WidthFixed,  22.f);
                        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 2.f);
                        ImGui::TableSetupColumn("Type",  ImGuiTableColumnFlags_WidthFixed,  48.f);
                        ImGui::TableSetupColumn("Hits",  ImGuiTableColumnFlags_WidthFixed,  90.f);
                        ImGui::TableHeadersRow();

                        for(int vi = 0; vi < (int)summary.editableValues.size(); ++vi){
                            const auto& value = summary.editableValues[(size_t)vi];
                            ImGui::PushID(vi);
                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);
                            if(value.kind == "Named"){
                                auto it = namedOverrides.find(value.value);
                                if(it == namedOverrides.end()){
                                    namedOverrides.emplace(value.value, value.value);
                                    it = namedOverrides.find(value.value);
                                }
                                std::string& chosen = it->second;
                                if(ImGui::ColorButton("##ncolbtn", namedTagColor(chosen),
                                        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                                        {18.f, 18.f}))
                                    ImGui::OpenPopup("##ncolpopup");
                                if(ImGui::BeginPopup("##ncolpopup")){
                                    ImGui::TextColored(Col::SUBTEXT, "Pick a named colour");
                                    ImGui::Separator();
                                    for(const auto& row : PageLocalization::NAMED_TAG_ROWS){
                                        ImVec4 rc{row.r, row.g, row.b, std::max(row.a, 0.15f)};
                                        ImGui::ColorButton(("##ts_" + std::string(row.name)).c_str(),
                                            rc, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                                            {14.f, 14.f});
                                        ImGui::SameLine();
                                        if(ImGui::Selectable(row.name, chosen == row.name))
                                            chosen = row.name;
                                    }
                                    ImGui::EndPopup();
                                }
                            } else {
                                auto it = rgbOverrides.find(value.value);
                                if(it == rgbOverrides.end()){
                                    std::array<float,3> rgb{};
                                    parseRgbValue(value.value, rgb.data());
                                    rgbOverrides.emplace(value.value, rgb);
                                    it = rgbOverrides.find(value.value);
                                }
                                ImGui::ColorEdit3("##rgbpick", it->second.data(),
                                    ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs);
                            }

                            ImGui::TableSetColumnIndex(1);
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextUnformatted(value.value.c_str());
                            if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                                drawColourTagTooltip(value);

                            ImGui::TableSetColumnIndex(2);
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextColored(Col::SUBTEXT, "%s", value.kind.c_str());

                            ImGui::TableSetColumnIndex(3);
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextColored(Col::SUBTEXT, "%d hits / %d files",
                                value.occurrences, value.fileCount);

                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                    ImGui::Spacing();
                }

                if(!summary.editableGradients.empty()){
                    SubLabel("Gradients");
                    static const char* kTypeItems[] = { "Smooth", "Triple", "Stepped" };
                    const float lineH  = ImGui::GetFrameHeightWithSpacing();
                    const float cardH  = lineH * 2.f + 6.f;
                    const int   shown  = std::min((int)summary.editableGradients.size(), 4);
                    if(ImGui::BeginChild("##gradscroll", {0.f, shown * cardH + 4.f}, false)){
                        for(int gi = 0; gi < (int)summary.editableGradients.size(); ++gi){
                            const auto& grad = summary.editableGradients[(size_t)gi];
                            ImGui::PushID(gi);

                            // Seed override state
                            auto& ov = gradientOverrides[grad.sequence];
                            if(!ov.seeded){
                                ov.seeded = true;
                                ov.outputType = grad.type;
                                const size_t pipe = grad.sequence.find('|');
                                ov.origStartValue = (pipe != std::string::npos)
                                    ? grad.sequence.substr(0, pipe) : grad.sequence;
                                const size_t lastPipe = grad.sequence.rfind('|');
                                ov.origEndValue = (lastPipe != std::string::npos)
                                    ? grad.sequence.substr(lastPipe + 1) : grad.sequence;
                                ov.origMidValue = grad.midValue;
                                float tmp[3] = {0.5f,0.5f,0.5f};
                                parseRgbValue(ov.origStartValue, tmp);
                                ov.startRgb = {tmp[0],tmp[1],tmp[2]};
                                parseRgbValue(ov.origEndValue, tmp);
                                ov.endRgb   = {tmp[0],tmp[1],tmp[2]};
                                if(!grad.midValue.empty()){
                                    parseRgbValue(grad.midValue, tmp);
                                    ov.midRgb = {tmp[0],tmp[1],tmp[2]};
                                }
                                // Build stepped palette in order of first appearance
                                {
                                    std::istringstream ss(grad.sequence);
                                    std::string step;
                                    while(std::getline(ss, step, '|')){
                                        if(step.empty()) continue;
                                        bool found = false;
                                        for(const auto& p : ov.origPalette) if(p == step){ found = true; break; }
                                        if(!found){
                                            ov.origPalette.push_back(step);
                                            parseRgbValue(step, tmp);
                                            ov.paletteRgb.push_back({tmp[0],tmp[1],tmp[2]});
                                        }
                                    }
                                }
                            }

                            // ── Row 1: detection info ─────────────────────────
                            ImGui::Checkbox("##grov", &ov.enabled);
                            if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                                ImGui::SetTooltip("Enable gradient override for export");
                            ImGui::SameLine(0.f, 6.f);

                            // Type badge (coloured)
                            const ImVec4 typeCol =
                                (grad.type == GradientType::Triple)  ? ImVec4{1.f,0.7f,0.2f,1.f} :
                                (grad.type == GradientType::Stepped) ? ImVec4{0.6f,0.4f,1.f,1.f} :
                                                                       ImVec4{0.3f,0.8f,1.f,1.f};
                            const char* typeLabel =
                                (grad.type == GradientType::Triple)  ? "Triple"  :
                                (grad.type == GradientType::Stepped) ? "Stepped" : "Smooth";
                            ImGui::TextColored(typeCol, "%s", typeLabel);
                            ImGui::SameLine(0.f, 8.f);

                            // Original colour swatches
                            if(grad.type == GradientType::Stepped){
                                // Show unique palette colours in order of first appearance
                                std::vector<std::string> palette;
                                {
                                    std::istringstream ss(grad.sequence);
                                    std::string step;
                                    while(std::getline(ss, step, '|')){
                                        if(step.empty()) continue;
                                        bool found = false;
                                        for(const auto& p : palette) if(p == step){ found = true; break; }
                                        if(!found) palette.push_back(step);
                                    }
                                }
                                for(int pi = 0; pi < (int)palette.size(); ++pi){
                                    float pc[3] = {0.5f,0.5f,0.5f};
                                    parseRgbValue(palette[(size_t)pi], pc);
                                    if(pi > 0) ImGui::SameLine(0.f, 2.f);
                                    char pid[16]; std::snprintf(pid, sizeof(pid), "##ogp%d", pi);
                                    ImGui::ColorButton(pid, {pc[0],pc[1],pc[2],1.f},
                                        ImGuiColorEditFlags_NoPicker|ImGuiColorEditFlags_NoTooltip|ImGuiColorEditFlags_NoBorder,{14.f,14.f});
                                }
                            } else {
                                float cs[3]={0.5f,0.5f,0.5f}, ce[3]={0.5f,0.5f,0.5f};
                                parseRgbValue(ov.origStartValue, cs);
                                parseRgbValue(ov.origEndValue,   ce);
                                ImGui::ColorButton("##ogs",{cs[0],cs[1],cs[2],1.f},
                                    ImGuiColorEditFlags_NoPicker|ImGuiColorEditFlags_NoTooltip|ImGuiColorEditFlags_NoBorder,{14.f,14.f});
                                if(grad.type == GradientType::Triple && !ov.origMidValue.empty()){
                                    float cm[3]={0.5f,0.5f,0.5f};
                                    parseRgbValue(ov.origMidValue, cm);
                                    ImGui::SameLine(0.f,2.f);
                                    ImGui::ColorButton("##ogm",{cm[0],cm[1],cm[2],1.f},
                                        ImGuiColorEditFlags_NoPicker|ImGuiColorEditFlags_NoTooltip|ImGuiColorEditFlags_NoBorder,{14.f,14.f});
                                }
                                ImGui::SameLine(0.f,2.f);
                                ImGui::TextUnformatted("\xe2\x86\x92");
                                ImGui::SameLine(0.f,2.f);
                                ImGui::ColorButton("##oge",{ce[0],ce[1],ce[2],1.f},
                                    ImGuiColorEditFlags_NoPicker|ImGuiColorEditFlags_NoTooltip|ImGuiColorEditFlags_NoBorder,{14.f,14.f});
                            }
                            ImGui::SameLine(0.f, 8.f);

                            // Sample text + stats pushed right
                            const char* sampleLabel = grad.sampleText.empty()
                                ? grad.preview.c_str() : grad.sampleText.c_str();
                            ImGui::TextUnformatted(sampleLabel);
                            ImGui::SameLine();
                            const float statsX = ImGui::GetContentRegionMax().x - 90.f;
                            if(ImGui::GetCursorPosX() < statsX) ImGui::SetCursorPosX(statsX);
                            ImGui::TextColored(Col::SUBTEXT, "%d steps | %dx", grad.steps, grad.occurrences);

                            // ── Row 2: replacement controls ───────────────────
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 22.f); // align under checkbox
                            if(!ov.enabled) ImGui::BeginDisabled();

                            // Output type combo
                            ImGui::SetNextItemWidth(76.f);
                            int typeIdx = (int)ov.outputType;
                            if(ImGui::Combo("##outtype", &typeIdx, kTypeItems, 3))
                                ov.outputType = (GradientType)typeIdx;
                            ImGui::SameLine(0.f, 6.f);

                            if(ov.outputType == GradientType::Stepped){
                                // One picker per palette colour
                                for(int pi = 0; pi < (int)ov.paletteRgb.size(); ++pi){
                                    char pid[16]; std::snprintf(pid, sizeof(pid), "##grp%d", pi);
                                    ColorPickerButton(pid, ov.paletteRgb[(size_t)pi].data());
                                    if(pi + 1 < (int)ov.paletteRgb.size()) ImGui::SameLine(0.f, 3.f);
                                }
                            } else {
                                ImGui::TextColored(Col::SUBTEXT, "Start");
                                ImGui::SameLine(0.f, 4.f);
                                ColorPickerButton("##grs", ov.startRgb.data());
                                if(ov.outputType == GradientType::Triple){
                                    ImGui::SameLine(0.f, 6.f);
                                    ImGui::TextColored(Col::SUBTEXT, "Mid");
                                    ImGui::SameLine(0.f, 4.f);
                                    ColorPickerButton("##grm", ov.midRgb.data());
                                }
                                ImGui::SameLine(0.f, 6.f);
                                ImGui::TextColored(Col::SUBTEXT, "End");
                                ImGui::SameLine(0.f, 4.f);
                                ColorPickerButton("##gre", ov.endRgb.data());
                            }

                            if(!ov.enabled) ImGui::EndDisabled();

                            ImGui::PopID();
                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::Spacing();
                        }
                    }
                    ImGui::EndChild();
                    ImGui::Spacing();
                }

            } else if(!isPremadeCacheBuilding()){
                SectionNote("No APB colour tags or gradients were detected in this template.");
            }
        }

        SectionLabel("Output");
        if(BeginSectionTable("##premadeoutput", 124.f, 110.f)){
            BeginSectionRow("Folder");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##premadeoutdir", outputDir, sizeof(outputDir));
            NextSectionAction();
            if(ImGui::Button("Browse##premadeout")){
                std::string s;
                if(BrowseFolder(s, "Select export folder"))
                    std::snprintf(outputDir, sizeof(outputDir), "%s", s.c_str());
            }

            BeginSectionRow("Helpers");
            if(ImGui::Button("Use Downloads##premadedl")){
                const std::string downloadDir = DownloadsDir() + "\\APB Premade Localization";
                std::snprintf(outputDir, sizeof(outputDir), "%s", downloadDir.c_str());
            }
            ImGui::SameLine();
            if(ImGui::Button("Open##premadeoutopen") && outputDir[0])
                OpenInExplorer(outputDir);
            EndSectionTable();
        }
        {
            const std::string src = premadeConfigLocation();
            const std::string exp = resolvedOutputPreview();
            ImGui::TextColored(Col::SUBTEXT, "Source: %s  |  Export: %s",
                src.empty() ? "(not set)" : src.c_str(),
                exp.empty() ? "(no folder set)" : exp.c_str());
        }

        if(!lastOutputDir.empty()){
            ImGui::Spacing();
            if(ImGui::Button("Open Last Export##premadelast"))
                OpenInExplorer(lastOutputDir);
        }

        ImGui::EndChild();
    }
};

} // namespace apb::gui
