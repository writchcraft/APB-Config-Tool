#pragma once
#include "App.h"
#include "backend/InventoryItemTypes.h"
#include "backend/Contacts.h"
#include "backend/PlayerRoles.h"
#include "backend/WeaponItemTypes.h"
#include "backend/VehicleItemTypes.h"
#include "backend/ArmasScraper.h"
#include "backend/AppDirs.h"
#include "backend/ImageLoader.h"
#include "backend/ThemeLibrary.h"
#include "../resource.h"
#include <d3d11.h>
#include <filesystem>
#include <cmath>
#include <iomanip>
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
    float gradStart[3] = {0.08f,0.00f,0.78f};
    float tripleMid[3] = {0.36f,0.08f,0.82f};
    float gradEnd[3]   = {0.65f,0.02f,0.40f};

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
        gradStart[0]=0.08f; gradStart[1]=0.00f; gradStart[2]=0.78f;
        tripleMid[0]=0.36f; tripleMid[1]=0.08f; tripleMid[2]=0.82f;
        gradEnd[0]=0.65f;   gradEnd[1]=0.02f;   gradEnd[2]=0.40f;
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

        SectionLabel("Colour Settings");

        // ── Scheme selector ───────────────────────────────────────────
        ImGui::Text("Style:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(220.f);
        char cid[32]; snprintf(cid,sizeof(cid),"##sc%s",uid);
        if(ImGui::BeginCombo(cid, currentStyleLabel(lib))){
            for(int i=0;i<BASE_COUNT;++i){
                if(ImGui::Selectable(BASE_SCHEMES[i], schemeIdx==i)){
                    schemeIdx=i;
                    presetEquipped=false;
                    presetThemeIdx=-1;
                    changed=true;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Text("Preset:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(220.f);
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
        ImGui::SameLine();
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
        char opfbid[40]; snprintf(opfbid,sizeof(opfbid),"Open Preset Folder##pr%s",uid);
        if(ImGui::SmallButton(opfbid)) OpenInExplorer(ThemeLibrary::themesDir());

        // ── Colour pickers (only for Single/manual Gradient) ──────────
        if(!presetEquipped && schemeIdx==2){
            ImGui::SameLine(); ImGui::Text("Colour:"); ImGui::SameLine();
            char pid[32]; snprintf(pid,sizeof(pid),"##sing%s",uid);
            if(ColorPickerButton(pid,singleCol)) changed=true;
        }
        if(!presetEquipped && schemeIdx==3){
            ImGui::SameLine(); ImGui::Text("Start:"); ImGui::SameLine();
            char p1[32],p2[32];
            snprintf(p1,sizeof(p1),"##gs%s",uid); snprintf(p2,sizeof(p2),"##ge%s",uid);
            if(ColorPickerButton(p1,gradStart)) changed=true;
            ImGui::SameLine(); ImGui::Text("End:"); ImGui::SameLine();
            if(ColorPickerButton(p2,gradEnd)) changed=true;
        }
        if(!presetEquipped && schemeIdx==4){
            ImGui::SameLine(); ImGui::Text("Start:"); ImGui::SameLine();
            char p1[32],pm[32],p2[32];
            snprintf(p1,sizeof(p1),"##gts%s",uid);
            snprintf(pm,sizeof(pm),"##gtm%s",uid);
            snprintf(p2,sizeof(p2),"##gte%s",uid);
            if(ColorPickerButton(p1,gradStart)) changed=true;
            ImGui::SameLine(); ImGui::Text("Middle:"); ImGui::SameLine();
            if(ColorPickerButton(pm,tripleMid)) changed=true;
            ImGui::SameLine(); ImGui::Text("End:"); ImGui::SameLine();
            if(ColorPickerButton(p2,gradEnd)) changed=true;
        }

        ImGui::Spacing();
        char defbid[32]; snprintf(defbid,sizeof(defbid),"Defaults##df%s",uid);
        if(ImGui::Button(defbid)){ resetDefaults(); changed=true; }
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

        auto row=[&](const char* label,char* buf,const char* id,const char* filter,bool isSave=false){
            ImGui::Text("%s",label); ImGui::SameLine();
            ImGui::SetNextItemWidth(-80);
            ImGui::InputText(id,buf,MAX_PATH);
            ImGui::SameLine();
            char bid[32]; snprintf(bid,sizeof(bid),"Browse##%s",id);
            if(ImGui::Button(bid)){
                std::string s;
                if(isSave) BrowseSaveFile(s,filter);
                else       BrowseFile(s,filter);
                if(!s.empty()) strncpy(buf,s.c_str(),MAX_PATH-1);
            }
        };
        row("GER File:    ",gerPath,"##iitger","All Files\0*.*\0");
        row("INT File:    ",intPath,"##iitint","All Files\0*.*\0");
        row("Output:      ",outPath,"##iitout","Text Files\0*.txt\0",true);

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
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
        ImGui::Spacing();
        std::string t=log.get();
        ImGui::InputTextMultiline("##iitlog",(char*)t.c_str(),t.size()+1,{-1,-1},ImGuiInputTextFlags_ReadOnly);
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

        ImGui::Text("INT File:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(-80);
        ImGui::InputText("##prolesint", intPath, MAX_PATH);
        ImGui::SameLine();
        if(ImGui::Button("Browse##prolesint")){
            std::string s;
            if(BrowseFile(s, "INT Files\0*.int;*.INT\0\0")) strncpy(intPath, s.c_str(), MAX_PATH - 1);
        }

        ImGui::Text("Output Location:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(-80);
        ImGui::InputText("##prolesout", outPath, MAX_PATH);
        ImGui::SameLine();
        if(ImGui::Button("Browse##prolesout")){
            std::string s;
            if(BrowseSaveFile(s, "INT Files\0*.int\0GER Files\0*.ger\0Text Files\0*.txt\0\0", "int"))
                strncpy(outPath, s.c_str(), MAX_PATH - 1);
        }
        ImGui::TextColored(Col::SUBTEXT, "Leave blank to create a new file in Downloads.");

        static constexpr const char* STYLE_LABELS[] = {
            "No Colour", "Solid", "Stepped", "Smooth", "Triple Gradient"
        };
        ImGui::Text("Role Name Style:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(180.f);
        if(ImGui::BeginCombo("##prolesstyle", STYLE_LABELS[(nameStyleIdx >= 0 && nameStyleIdx < 5) ? nameStyleIdx : 1])){
            for(int i = 0; i < 5; ++i){
                const bool selected = (nameStyleIdx == i);
                if(ImGui::Selectable(STYLE_LABELS[i], selected)) nameStyleIdx = i;
                if(selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if(nameStyleIdx == 1){
            ImGui::SameLine();
            ImGui::Text("Colour:"); ImGui::SameLine();
            ColorPickerButton("##prolessolid", solidCol);
        } else if(nameStyleIdx == 2 || nameStyleIdx == 3){
            ImGui::SameLine();
            ImGui::Text("Start:"); ImGui::SameLine();
            ColorPickerButton("##prolesstart", gradStart);
            ImGui::SameLine();
            ImGui::Text("End:"); ImGui::SameLine();
            ColorPickerButton("##prolesend", gradEnd);
        } else if(nameStyleIdx == 4){
            ImGui::SameLine();
            ImGui::Text("Start:"); ImGui::SameLine();
            ColorPickerButton("##prolestri_start", gradStart);
            ImGui::SameLine();
            ImGui::Text("Middle:"); ImGui::SameLine();
            ColorPickerButton("##prolestri_mid", gradMiddle);
            ImGui::SameLine();
            ImGui::Text("End:"); ImGui::SameLine();
            ColorPickerButton("##prolestri_end", gradEnd);
        }

        ImGui::Checkbox("Use short equipment names", &useShortEquipmentNames);

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::ProgressBar(prog.frac(), {-1, 18});
        ImGui::TextColored(Col::SUBTEXT, "%d / %d  %s",
            prog.done.load(), prog.total.load(), prog.lbl().c_str());
        ImGui::Spacing();

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

        ImGui::Spacing();
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

        ImGui::Text("Source File:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(-80);
        ImGui::InputText("##contactsint", intPath, MAX_PATH);
        ImGui::SameLine();
        if(ImGui::Button("Browse##contactsint")){
            std::string s;
            if(BrowseFile(s, "Localization Files\0*.int;*.INT;*.ger;*.GER\0\0"))
                strncpy(intPath, s.c_str(), MAX_PATH - 1);
        }

        ImGui::Text("Output Location:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(-80);
        ImGui::InputText("##contactsout", outPath, MAX_PATH);
        ImGui::SameLine();
        if(ImGui::Button("Browse##contactsout")){
            std::string s;
            if(BrowseSaveFile(s, "INT Files\0*.int\0GER Files\0*.ger\0Text Files\0*.txt\0\0", "int"))
                strncpy(outPath, s.c_str(), MAX_PATH - 1);
        }
        ImGui::TextColored(Col::SUBTEXT, "Leave blank to create a new file in Downloads.");

        static constexpr const char* MODE_LABELS[] = {"UNLOCKS", "MISSIONS"};
        ImGui::Text("Mode:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(180.f);
        if(ImGui::BeginCombo("##contactsmode", MODE_LABELS[(modeIdx >= 0 && modeIdx < 2) ? modeIdx : 0])){
            for(int i = 0; i < 2; ++i){
                const bool selected = (modeIdx == i);
                if(ImGui::Selectable(MODE_LABELS[i], selected)) modeIdx = i;
                if(selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::ProgressBar(prog.frac(), {-1, 18});
        ImGui::TextColored(Col::SUBTEXT, "%d / %d  %s",
            prog.done.load(), prog.total.load(), prog.lbl().c_str());
        ImGui::Spacing();

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

        ImGui::Spacing();
        std::string t = log.get();
        ImGui::InputTextMultiline("##contactslog", (char*)t.c_str(), t.size() + 1, {-1, -1},
            ImGuiInputTextFlags_ReadOnly);
        ImGui::EndChild();
    }
};

// ══════════════════════════════════════════════════════════════════════════
// PageWeaponItemTypes
// ══════════════════════════════════════════════════════════════════════════
struct PageWeaponItemTypes {
    char   intPath[MAX_PATH]={}, outPath[MAX_PATH]={};
    int    workers=8;
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
        int wk=workers;
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

        // File inputs
        ImGui::Text("INT File:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(-80); ImGui::InputText("##witint",intPath,MAX_PATH);
        ImGui::SameLine();
        if(ImGui::Button("Browse##wit")){ std::string s;if(BrowseFile(s,"INT Files\0*.int;*.INT\0\0"))strncpy(intPath,s.c_str(),MAX_PATH-1);}

        ImGui::Text("Output Folder:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(-80); ImGui::InputText("##witout",outPath,MAX_PATH);
        ImGui::SameLine();
        if(ImGui::Button("Browse##witout")){ std::string s; if(BrowseFolder(s,"Select output folder")) strncpy(outPath,s.c_str(),MAX_PATH-1); }

        ImGui::Text("Workers: "); ImGui::SameLine();
        ImGui::SetNextItemWidth(80); ImGui::InputInt("##witwk",&workers); workers=std::max(1,std::min(64,workers));

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        // Colour settings
        bool colChanged=false;
        colours.draw("wit",colChanged);

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        // Progress
        ImGui::ProgressBar(prog.frac(),{-1,18});
        ImGui::TextColored(Col::SUBTEXT,"%d / %d  %s",prog.done.load(),prog.total.load(),prog.lbl().c_str());
        ImGui::Spacing();

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
        ImGui::InputTextMultiline("##witlog",(char*)t.c_str(),t.size()+1,{-1,100},ImGuiInputTextFlags_ReadOnly);
        ImGui::EndChild();
    }
};

// ══════════════════════════════════════════════════════════════════════════
// PageVehicleItemTypes
// ══════════════════════════════════════════════════════════════════════════
struct PageVehicleItemTypes {
    char   intPath[MAX_PATH]={}, outPath[MAX_PATH]={};
    int    workers=8;
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
        int wk=workers;
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

        ImGui::Text("INT File:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(-80); ImGui::InputText("##vitint",intPath,MAX_PATH);
        ImGui::SameLine();
        if(ImGui::Button("Browse##vit")){ std::string s;if(BrowseFile(s,"INT Files\0*.int;*.INT\0\0"))strncpy(intPath,s.c_str(),MAX_PATH-1);}

        ImGui::Text("Output Folder:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(-80); ImGui::InputText("##vitout",outPath,MAX_PATH);
        ImGui::SameLine();
        if(ImGui::Button("Browse##vitout")){ std::string s; if(BrowseFolder(s,"Select output folder")) strncpy(outPath,s.c_str(),MAX_PATH-1); }

        ImGui::Text("Workers: "); ImGui::SameLine();
        ImGui::SetNextItemWidth(80); ImGui::InputInt("##vitwk",&workers); workers=std::max(1,std::min(64,workers));

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        bool colChanged=false;
        colours.draw("vit",colChanged);

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        ImGui::ProgressBar(prog.frac(),{-1,18});
        ImGui::TextColored(Col::SUBTEXT,"%d / %d  %s",prog.done.load(),prog.total.load(),prog.lbl().c_str());
        ImGui::Spacing();

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
        ImGui::InputTextMultiline("##vitlog",(char*)t.c_str(),t.size()+1,{-1,100},ImGuiInputTextFlags_ReadOnly);
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
        ImGui::Text("ID Range:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(90); ImGui::InputInt("##asst",&startId);
        ImGui::SameLine(); ImGui::Text("\xe2\x86\x92"); ImGui::SameLine();
        ImGui::SetNextItemWidth(90); ImGui::InputInt("##asen",&endId);
        ImGui::Text("Threads: "); ImGui::SameLine();
        ImGui::SetNextItemWidth(90); ImGui::InputInt("##asth",&threads); threads=std::max(1,std::min(128,threads));
        ImGui::Text("Output:  "); ImGui::SameLine();
        ImGui::SetNextItemWidth(-80); ImGui::InputText("##asop",outPath,MAX_PATH);
        ImGui::SameLine();
        if(ImGui::Button("Browse##as")){ std::string s;if(BrowseSaveFile(s,"Text Files\0*.txt\0\0","txt"))strncpy(outPath,s.c_str(),MAX_PATH-1);}
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::ProgressBar(prog.frac(),{-1,18});
        ImGui::TextColored(Col::SUBTEXT,"%d / %d  %s",prog.done.load(),prog.total.load(),prog.lbl().c_str());
        ImGui::Spacing();
        bool busy=running.load();
        if(!busy){
            if(RunButton("Start##as")) startAction();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,Col::RED);
            if(ImGui::Button("Stop",{120,32})) cancelAction();
            ImGui::PopStyleColor();
            ImGui::SameLine(); ImGui::TextColored(Col::YELLOW,"Scanning...");
        }
        ImGui::Spacing();
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
        std::snprintf(apbTag, sizeof(apbTag),
            "<Color:R=%.6f G=%.6f B=%.6f>", rgb[0], rgb[1], rgb[2]);
    }

    void draw(){
        ImGui::BeginChild("##hexconv",{0,0},false);
        SectionLabel("Hex Converter");
        ImGui::TextWrapped("Enter RGB numbers below, or paste an APB colour tag into the APB Tag field and the first R/G/B value set will be detected automatically.");
        ImGui::Spacing();

        ImGui::TextUnformatted("<Color:R={number} G={number} B={number}>");
        ImGui::Spacing();

        bool rgbEdited = false;
        ImGui::Text("R:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(110.f);
        rgbEdited |= ImGui::InputFloat("##hexr", &rgb[0], 0.f, 0.f, "%.6f");
        ImGui::SameLine();
        ImGui::Text("G:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(110.f);
        rgbEdited |= ImGui::InputFloat("##hexg", &rgb[1], 0.f, 0.f, "%.6f");
        ImGui::SameLine();
        ImGui::Text("B:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(110.f);
        rgbEdited |= ImGui::InputFloat("##hexb", &rgb[2], 0.f, 0.f, "%.6f");
        if(rgbEdited) updateTagFromRgb();

        ImGui::Spacing();
        ImGui::Text("APB Tag:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-70.f);
        if(ImGui::InputText("##hextagpreview", apbTag, sizeof(apbTag))){
            float parsed[3];
            if(firstTag(apbTag, parsed)){
                rgb[0] = parsed[0];
                rgb[1] = parsed[1];
                rgb[2] = parsed[2];
                updateTagFromRgb();
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("Copy##hextag"))
            ImGui::SetClipboardText(apbTag);

        output = toHex(rgb[0], rgb[1], rgb[2]);

        ImGui::Spacing();
        ImGui::Text("Hex Code:");
        ImGui::SameLine();
        if(ImGui::Button("Copy##hexconv"))
            ImGui::SetClipboardText(output.c_str());

        ImGui::PushStyleColor(ImGuiCol_FrameBg, Col::ITEM_BG);
        ImGui::InputTextMultiline("##hexout", output.data(), output.size() + 1, {-1.f, 52.f}, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();

        ImGui::TextColored(Col::SUBTEXT, "Values from 0-1 are normalized RGB; values above 1 are treated as 0-255 RGB.");
        ImGui::EndChild();
    }
};

// ══════════════════════════════════════════════════════════════════════════
// PageLocalization
// ══════════════════════════════════════════════════════════════════════════
struct PageLocalization {
    void draw(){
        ImGui::BeginChild("##loc",{0,0},false);
        SectionLabel("Localization Reference");
        if(ImGui::BeginTabBar("##loctabs")){
            if(ImGui::BeginTabItem("Symbols")){
                ImGui::TextWrapped("APB Inline Characters\n\n"
                    "\xe2\x86\xb5 (U+21B5)  Line break inside a localization value.\n"
                    "             Renders as a newline in-game.\n\n"
                    "&  Ampersand literal.\n"
                    "|  Vertical bar; separates tooltip sections in some contexts.");
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Fonts")){
                ImGui::TextWrapped("APB Font Tags — wrap text to change in-game appearance:");
                ImGui::Spacing();
                static const char* fonts[]={
                    "<Fonts:APBMenus_Font.APB_Helvetica_Regular_11>",
                    "<Fonts:APBMenus_Font.APB_Helvetica_Regular_12>",
                    "<Fonts:APBMenus_Font.APB_Helvetica_Regular_14>",
                    "<Fonts:APBMenus_Font.APB_Helvetica_Regular_16>",
                    "<Fonts:APBMenus_Font.APB_Helvetica_Bold_11>",
                    "<Fonts:APBMenus_Font.APB_Helvetica_Bold_13>",
                    "<Fonts:APBMenus_Font.APB_Helvetica_Bold_14>",
                    "<Fonts:APBMenus_Font.APB_Helvetica_Bold_24>",
                    "<Fonts:EngineFonts.TinyFont>",
                    "<Fonts:EngineFonts.SmallFont>",
                    "<Fonts:EngineFonts.MediumFont>",
                    "<Fonts:EngineFonts.LargeFont>",
                };
                for(auto f:fonts){ if(ImGui::Selectable(f)) ImGui::SetClipboardText(f); }
                ImGui::TextColored(Col::SUBTEXT,"(click to copy)");
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Colour Codes")){
                ImGui::TextWrapped(
                    "Named colour:\n"
                    "  <col:TagName>Text</col>\n\n"
                    "Float RGB colour:\n"
                    "  <Color:R=1.000 G=0.000 B=0.000>Text<Color:/>\n\n"
                    "Values are normalised floats (0.0-1.0).\n\n"
                    "Common named colours: White, Black, Red, Green, Blue,\n"
                    "Yellow, Orange, Purple, Cyan, Grey");
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
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text,Col::YELLOW);
        ImGui::TextUnformatted("APB Config Tool");
        ImGui::PopStyleColor();
        ImGui::TextWrapped("An open-source localisation and configuration utility for All Points Bulletin: Reloaded.");
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::Text("Author");        ImGui::SameLine(120); ImGui::TextUnformatted("writch");
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::Text("Credits");       ImGui::SameLine(120); ImGui::TextWrapped("Mewpri - original creator of the ItemTypes stat maker");
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
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
    void draw(){
        ImGui::BeginChild("##settings",{0,0},false);
        SectionLabel("Settings");

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

        ImGui::EndChild();
    }
};

} // namespace apb::gui
