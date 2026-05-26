#pragma once
// ============================================================
//  APB Config Tool  –  ImGui + DirectX 11 frontend
//  include/gui/App.h
// ============================================================

#include <d3d11.h>
#include <string>
#include <vector>
#include <array>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>
#include <queue>

// ImGui MUST be included before we define ImVec4 variables
#include "imgui.h"

namespace apb {
    struct RGB;
}

namespace apb::gui {

// ─────────────────────────────────────────────────────────────
//  Palette constants
//  ImVec4 has no constexpr constructor in MSVC — use inline const
//  with explicit constructor syntax (not brace-init).
// ─────────────────────────────────────────────────────────────
inline const ImVec4 COL_YELLOW     ( 0.969f, 0.820f, 0.039f, 1.f );
inline const ImVec4 COL_DARK_BG   ( 0.043f, 0.043f, 0.047f, 1.f );
inline const ImVec4 COL_PANEL_BG  ( 0.059f, 0.063f, 0.071f, 1.f );
inline const ImVec4 COL_ACCENT    ( 0.078f, 0.082f, 0.094f, 1.f );
inline const ImVec4 COL_HOVER     ( 0.110f, 0.118f, 0.133f, 1.f );
inline const ImVec4 COL_BORDER    ( 0.851f, 0.851f, 0.851f, 1.f );
inline const ImVec4 COL_BORDER_DIM( 0.165f, 0.169f, 0.184f, 1.f );
inline const ImVec4 COL_TEXT      ( 0.945f, 0.945f, 0.945f, 1.f );
inline const ImVec4 COL_SUBTEXT   ( 0.812f, 0.812f, 0.812f, 1.f );

// ─────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────
ImVec4   rgbToImVec4(const apb::RGB& c);
apb::RGB imVec4ToRgb(const ImVec4& v);

struct LogQueue {
    std::mutex mtx;
    std::queue<std::string> lines;
    void push(const std::string& s) { std::lock_guard<std::mutex> lk(mtx); lines.push(s); }
    bool pop(std::string& out)      { std::lock_guard<std::mutex> lk(mtx); if(lines.empty())return false; out=lines.front(); lines.pop(); return true; }
};

bool ColorChipButton(const char* id, float col[3], float w=28.f, float h=20.f);

// ─────────────────────────────────────────────────────────────
//  Page interface
// ─────────────────────────────────────────────────────────────
struct IPage {
    virtual ~IPage() = default;
    virtual void draw()            = 0;
    virtual void restoreDefaults() = 0;
    virtual void applySettings()   = 0;
    virtual const char* title()    = 0;
};

IPage* makeGradientMakerPage();
IPage* makeWeaponColourPage();
IPage* makeInventoryItemTypesPage();
IPage* makeVehicleItemTypesPage();
IPage* makeWeaponItemTypesPage();
IPage* makeCreditsPage();
IPage* makeLocalizationPage();

// ─────────────────────────────────────────────────────────────
//  Application
// ─────────────────────────────────────────────────────────────
class App {
public:
    App();
    ~App();

    bool init(HWND hwnd, ID3D11Device* dev, ID3D11DeviceContext* ctx);
    void shutdown();
    void render();

private:
    void drawSideRail();
    void drawTitleBar();
    void drawBottomBar();
    void switchPage(int idx);

    struct Section {
        const char*              header;
        std::vector<const char*> labels;
        std::vector<int>         indices;
        int                      lastIdx = -1;
        bool                     open    = false;
    };

    std::vector<Section> m_sections;
    std::vector<IPage*>  m_pages;
    int                  m_current     = 0;
    bool                 m_initialised = false;
};

} // namespace apb::gui
