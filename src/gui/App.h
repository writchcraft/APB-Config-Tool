#pragma once
// ── APBTool ImGui App ─────────────────────────────────────────────────────
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include "imgui.h"

// ═══════════════════════════════════════════════════════════════════════════
//  APB-accurate colour palette  (sampled from the Options menu screenshot)
// ═══════════════════════════════════════════════════════════════════════════
namespace Col {
    // Backgrounds — matched to APB Options screenshot
    inline ImVec4 BG         {0.047f,0.047f,0.047f,1.f};  // #0c0c0c title/outer
    inline ImVec4 PANEL      {0.016f,0.016f,0.016f,1.f};  // #040404 content pane
    inline ImVec4 RAIL       {0.047f,0.047f,0.047f,1.f};  // #0c0c0c side rail
    inline ImVec4 ITEM_BG    {0.075f,0.075f,0.075f,1.f};  // #131313 input fields
    inline ImVec4 ITEM_HOV   {0.118f,0.118f,0.118f,1.f};  // #1e1e1e hover
    inline ImVec4 BOTTOM_BAR {0.125f,0.125f,0.125f,1.f};  // #202020 bottom bar

    // Accents
    inline ImVec4 YELLOW     {0.969f,0.820f,0.039f,1.f};  // #f7d10a
    inline ImVec4 YELLOW_DIM {0.706f,0.588f,0.020f,1.f};  // hover/dim
    inline ImVec4 YELLOW_TEXT{0.969f,0.820f,0.039f,1.f};

    // Text
    inline ImVec4 TEXT       {0.898f,0.898f,0.898f,1.f};  // #e5e5e5
    inline ImVec4 SUBTEXT    {0.627f,0.627f,0.627f,1.f};  // #a0a0a0
    inline ImVec4 LABEL      {0.800f,0.800f,0.800f,1.f};

    // Borders
    inline ImVec4 BORDER     {0.420f,0.420f,0.420f,1.f};  // #6b6b6b
    inline ImVec4 BORDER_DIM {0.235f,0.235f,0.235f,1.f};  // #3c3c3c

    // Rail
    inline ImVec4 RAIL_SEL   {0.969f,0.820f,0.039f,1.f};
    inline ImVec4 RAIL_HOV   {0.149f,0.149f,0.149f,1.f};

    // Buttons
    inline ImVec4 BTN_DARK   {0.239f,0.239f,0.239f,1.f};  // #3d3d3d
    inline ImVec4 BTN_HOV    {0.310f,0.310f,0.310f,1.f};  // #4f4f4f
    inline ImVec4 BTN_OK     {0.969f,0.820f,0.039f,1.f};

    // Status
    inline ImVec4 RED        {0.847f,0.149f,0.149f,1.f};
    inline ImVec4 GREEN      {0.176f,0.698f,0.275f,1.f};
}

// ── Page IDs ─────────────────────────────────────────────────────────────
enum class Page {
    GradientMaker=0, WeaponColour,
    InventoryItemTypes, WeaponItemTypes, VehicleItemTypes,
    ArmasScrape, Localization, Credits, COUNT
};

// ── Thread-safe log ───────────────────────────────────────────────────────
struct ThreadLog {
    std::mutex mu;
    std::string buf;
    void append(const std::string& s){ std::lock_guard<std::mutex>lk(mu); buf+=s+"\n"; }
    void clear()  { std::lock_guard<std::mutex>lk(mu); buf.clear(); }
    std::string get(){ std::lock_guard<std::mutex>lk(mu); return buf; }
};

// ── Progress ──────────────────────────────────────────────────────────────
struct Progress {
    std::atomic<int> done{0}, total{0};
    std::mutex mu;
    std::string label;
    void reset(int t){ done=0; total=t; std::lock_guard<std::mutex>lk(mu); label.clear(); }
    void set(int d,int t,const std::string& s){
        done=d; total=t; std::lock_guard<std::mutex>lk(mu); label=s;
    }
    float frac(){ int t=total.load(); return t>0?(float)done.load()/t:0.f; }
    std::string lbl(){ std::lock_guard<std::mutex>lk(mu); return label; }
};

// ═══════════════════════════════════════════════════════════════════════════
//  StyleAPB  –  sets ImGui colours to match APB Options menu
// ═══════════════════════════════════════════════════════════════════════════
inline void StyleAPB(){
    ImGuiStyle& s = ImGui::GetStyle();

    // Geometry
    s.WindowRounding    = 0.f;
    s.ChildRounding     = 0.f;
    s.FrameRounding     = 0.f;
    s.PopupRounding     = 0.f;
    s.ScrollbarRounding = 0.f;
    s.GrabRounding      = 0.f;
    s.TabRounding       = 0.f;

    s.WindowBorderSize  = 1.f;
    s.FrameBorderSize   = 1.f;
    s.PopupBorderSize   = 1.f;
    s.ChildBorderSize   = 1.f;
    s.WindowMenuButtonPosition = ImGuiDir_None;

    s.WindowPadding     = {10.f, 10.f};
    s.FramePadding      = {7.f,  4.f};
    s.ItemSpacing       = {8.f,  5.f};
    s.ItemInnerSpacing  = {6.f,  4.f};
    s.ScrollbarSize     = 16.f;
    s.GrabMinSize       = 10.f;
    s.IndentSpacing     = 14.f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]             = Col::PANEL;
    c[ImGuiCol_ChildBg]              = Col::PANEL;
    c[ImGuiCol_PopupBg]              = Col::ITEM_BG;
    c[ImGuiCol_Border]               = Col::BORDER;
    c[ImGuiCol_BorderShadow]         = {0,0,0,0};

    c[ImGuiCol_FrameBg]              = Col::ITEM_BG;
    c[ImGuiCol_FrameBgHovered]       = Col::ITEM_HOV;
    c[ImGuiCol_FrameBgActive]        = Col::ITEM_HOV;

    c[ImGuiCol_TitleBg]              = Col::BG;
    c[ImGuiCol_TitleBgActive]        = Col::BG;
    c[ImGuiCol_TitleBgCollapsed]     = Col::BG;
    c[ImGuiCol_MenuBarBg]            = Col::BG;

    c[ImGuiCol_ScrollbarBg]          = Col::BG;
    c[ImGuiCol_ScrollbarGrab]        = {0.82f,0.82f,0.82f,1.f};
    c[ImGuiCol_ScrollbarGrabHovered] = {0.92f,0.92f,0.92f,1.f};
    c[ImGuiCol_ScrollbarGrabActive]  = {1.00f,1.00f,1.00f,1.f};

    c[ImGuiCol_CheckMark]            = Col::YELLOW;
    c[ImGuiCol_SliderGrab]           = {0.85f,0.85f,0.85f,1.f};
    c[ImGuiCol_SliderGrabActive]     = {1.00f,1.00f,1.00f,1.f};

    c[ImGuiCol_Button]               = Col::BTN_DARK;
    c[ImGuiCol_ButtonHovered]        = Col::BTN_HOV;
    c[ImGuiCol_ButtonActive]         = {0.36f,0.36f,0.36f,1.f};

    c[ImGuiCol_Header]               = Col::ITEM_BG;
    c[ImGuiCol_HeaderHovered]        = Col::ITEM_HOV;
    c[ImGuiCol_HeaderActive]         = Col::ITEM_HOV;

    c[ImGuiCol_Separator]            = Col::BORDER;
    c[ImGuiCol_SeparatorHovered]     = Col::BORDER;
    c[ImGuiCol_SeparatorActive]      = Col::BORDER;

    c[ImGuiCol_ResizeGrip]           = {0,0,0,0};
    c[ImGuiCol_ResizeGripHovered]    = Col::YELLOW;
    c[ImGuiCol_ResizeGripActive]     = Col::YELLOW;

    c[ImGuiCol_Tab]                  = Col::BG;
    c[ImGuiCol_TabHovered]           = Col::ITEM_HOV;
    c[ImGuiCol_TabActive]            = Col::ITEM_BG;
    c[ImGuiCol_TabUnfocused]         = Col::BG;
    c[ImGuiCol_TabUnfocusedActive]   = Col::ITEM_BG;

    c[ImGuiCol_Text]                 = Col::TEXT;
    c[ImGuiCol_TextDisabled]         = Col::SUBTEXT;
    c[ImGuiCol_PlotLines]            = Col::YELLOW;
    c[ImGuiCol_PlotHistogram]        = Col::YELLOW;
}

// ═══════════════════════════════════════════════════════════════════════════
//  UI helpers matching APB Options visual style
// ═══════════════════════════════════════════════════════════════════════════

// Yellow bold section label — matches APB "General", "Resolution" headers exactly
inline void SectionLabel(const char* label){
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, Col::YELLOW);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    // APB uses a subtle horizontal rule under section headers
    ImGui::PushStyleColor(ImGuiCol_Separator, {0.30f,0.25f,0.04f,1.f});
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

// Subsection label (lighter weight)
inline void SubLabel(const char* label){
    ImGui::PushStyleColor(ImGuiCol_Text, Col::LABEL);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
}

// Colour swatch button that opens a picker popup
inline bool ColorPickerButton(const char* id, float col[3], float w=28.f, float h=22.f){
    ImVec4 c={col[0],col[1],col[2],1.f};
    ImGui::PushStyleColor(ImGuiCol_Button,       c);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,c);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, c);
    ImGui::PushStyleColor(ImGuiCol_Border,       Col::BORDER);
    bool clicked = ImGui::Button(id,{w,h});
    ImGui::PopStyleColor(4);
    if(clicked) ImGui::OpenPopup(id);
    bool changed=false;
    if(ImGui::BeginPopup(id)){
        changed=ImGui::ColorPicker3(id,col,
            ImGuiColorEditFlags_NoAlpha|ImGuiColorEditFlags_PickerHueWheel);
        ImGui::EndPopup();
    }
    return changed;
}

// Yellow "Run" style button (matches APB OK button)
inline bool RunButton(const char* label="Run", float w=120.f){
    ImGui::PushStyleColor(ImGuiCol_Button,        Col::BTN_OK);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Col::YELLOW_DIM);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Col::YELLOW_DIM);
    ImGui::PushStyleColor(ImGuiCol_Text,          {0.04f,0.04f,0.04f,1.f});
    ImGui::PushStyleColor(ImGuiCol_Border,        Col::YELLOW_DIM);
    bool pressed = ImGui::Button(label,{w,28.f});
    ImGui::PopStyleColor(5);
    return pressed;
}

// Browse button — compact, matches the input row style
inline bool BrowseButton(const char* id="Browse"){
    ImGui::PushStyleColor(ImGuiCol_Button,        Col::BTN_DARK);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Col::BTN_HOV);
    ImGui::PushStyleColor(ImGuiCol_Border,        Col::BORDER);
    bool pressed=ImGui::Button(id,{64.f,0.f});
    ImGui::PopStyleColor(3);
    return pressed;
}

// Best-effort APB INT folder detection (Steam installs on C:/D:/etc).
inline const std::string& DetectApbIntDir(){
    static const std::string cached = [](){
        namespace fs = std::filesystem;
        std::vector<std::string> candidates;
        auto add = [&](const std::string& p){ if(!p.empty()) candidates.push_back(p); };

        if(const char* pf86 = std::getenv("ProgramFiles(x86)"))
            add(std::string(pf86) + "\\Steam\\steamapps\\common\\APB Reloaded\\APBGame\\Localization\\INT");
        if(const char* pf   = std::getenv("ProgramFiles"))
            add(std::string(pf) + "\\Steam\\steamapps\\common\\APB Reloaded\\APBGame\\Localization\\INT");

        for(char d='C'; d<='Z'; ++d){
            char root[] = {d,':','\\','\0'};
            if(GetDriveTypeA(root)==DRIVE_NO_ROOT_DIR) continue;
            std::string r = root;
            add(r + "Steam\\steamapps\\common\\APB Reloaded\\APBGame\\Localization\\INT");
            add(r + "SteamLibrary\\steamapps\\common\\APB Reloaded\\APBGame\\Localization\\INT");
            add(r + "Games\\Steam\\steamapps\\common\\APB Reloaded\\APBGame\\Localization\\INT");
            add(r + "Program Files (x86)\\Steam\\steamapps\\common\\APB Reloaded\\APBGame\\Localization\\INT");
        }

        for(const auto& p : candidates){
            std::error_code ec;
            if(fs::exists(p,ec) && fs::is_directory(p,ec)) return p;
        }
        return std::string{};
    }();
    return cached;
}

inline std::string DetectApbIntFile(const char* fileName){
    if(!fileName || !*fileName) return {};
    namespace fs = std::filesystem;
    const std::string& intDir = DetectApbIntDir();
    if(intDir.empty()) return {};

    fs::path exact = fs::path(intDir) / fileName;
    std::error_code ec;
    if(fs::exists(exact, ec) && fs::is_regular_file(exact, ec))
        return exact.string();

    const std::string wanted = fileName;
    for(const auto& entry : fs::directory_iterator(intDir, ec)){
        if(ec) break;
        if(!entry.is_regular_file(ec)) continue;
        const std::string name = entry.path().filename().string();
        if(_stricmp(name.c_str(), wanted.c_str()) == 0)
            return entry.path().string();
    }
    return {};
}

// File browse dialogs
inline bool BrowseFile(std::string& out, const char* filter="All Files\0*.*\0"){
    char buf[MAX_PATH]={};
    OPENFILENAMEA ofn={};
    std::string initialDir;
    if(filter && (std::strstr(filter,"*.int") || std::strstr(filter,"*.INT")))
        initialDir = DetectApbIntDir();

    ofn.lStructSize=sizeof(ofn);
    ofn.lpstrFilter=filter;
    ofn.lpstrFile=buf; ofn.nMaxFile=MAX_PATH;
    ofn.lpstrInitialDir = initialDir.empty() ? nullptr : initialDir.c_str();
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameA(&ofn)){out=buf;return true;}
    return false;
}
inline bool BrowseFolder(std::string& out, const char* title="Select folder"){
    BROWSEINFOA bi{};
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderA(&bi);
    if(!pidl) return false;
    char path[MAX_PATH]={};
    bool ok = SHGetPathFromIDListA(pidl, path) == TRUE;
    CoTaskMemFree(pidl);
    if(ok){ out = path; return true; }
    return false;
}
inline bool BrowseSaveFile(std::string& out, const char* filter="All Files\0*.*\0", const char* defExt=nullptr){
    char buf[MAX_PATH]={};
    OPENFILENAMEA ofn={};
    ofn.lStructSize=sizeof(ofn);
    ofn.lpstrFilter=filter;
    ofn.lpstrFile=buf; ofn.nMaxFile=MAX_PATH;
    ofn.lpstrDefExt=defExt;
    ofn.Flags=OFN_OVERWRITEPROMPT|OFN_NOCHANGEDIR;
    if(GetSaveFileNameA(&ofn)){out=buf;return true;}
    return false;
}
inline std::string DownloadsDir(){
    PWSTR wsz = nullptr;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DEFAULT, nullptr, &wsz)) && wsz){
        int n = WideCharToMultiByte(CP_UTF8, 0, wsz, -1, nullptr, 0, nullptr, nullptr);
        std::string out;
        if(n > 0){
            out.resize((size_t)n);
            WideCharToMultiByte(CP_UTF8, 0, wsz, -1, out.data(), n, nullptr, nullptr);
            if(!out.empty() && out.back()=='\0') out.pop_back();
        }
        CoTaskMemFree(wsz);
        if(!out.empty()) return out;
    }
    const char* up = std::getenv("USERPROFILE");
    if(up) return std::string(up) + "\\Downloads";
    return ".";
}
inline void OpenInExplorer(const std::string& path){
    ShellExecuteA(nullptr,"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
}
