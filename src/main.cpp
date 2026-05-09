// src/main.cpp  – Win32 + DX11 entry point for APB Config Tool
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "ole32.lib")

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

#include "resource.h"

// Forward
namespace apb::gui { void Render(); }
namespace apb::gui { ImFont* ResolvePreviewFont(const char* apbFontTag, float* outPixelSize); }

// ── D3D11 globals — exposed so ImageLoader can upload textures ────────────
ID3D11Device*           g_dev  = nullptr;
ID3D11DeviceContext*    g_ctx  = nullptr;
static IDXGISwapChain*         g_sc   = nullptr;
static ID3D11RenderTargetView* g_rtv  = nullptr;

static void CreateRTV(){
    ID3D11Texture2D* bb=nullptr;
    g_sc->GetBuffer(0,IID_PPV_ARGS(&bb));
    if(bb){ g_dev->CreateRenderTargetView(bb,nullptr,&g_rtv); bb->Release(); }
}
static void DropRTV(){ if(g_rtv){ g_rtv->Release(); g_rtv=nullptr; } }

static bool InitDX(HWND hw){
    DXGI_SWAP_CHAIN_DESC sd={};
    sd.BufferCount=2; sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate={60,1};
    sd.SampleDesc.Count=1; sd.OutputWindow=hw; sd.Windowed=TRUE;
    sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags=DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    D3D_FEATURE_LEVEL fl[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    if(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,
        fl,2,D3D11_SDK_VERSION,&sd,&g_sc,&g_dev,&got,&g_ctx)!=S_OK) return false;
    CreateRTV(); return true;
}
static void ShutDX(){
    DropRTV();
    if(g_sc)  { g_sc->Release();  g_sc=nullptr;  }
    if(g_ctx) { g_ctx->Release(); g_ctx=nullptr;  }
    if(g_dev) { g_dev->Release(); g_dev=nullptr;  }
}

// ── Returns path to Documents\APBConfigTool ───────────────────────────────
static std::string GetAppDocumentsDir(){
    char buf[MAX_PATH]={};
    if(SUCCEEDED(SHGetFolderPathA(nullptr,CSIDL_PERSONAL,nullptr,SHGFP_TYPE_CURRENT,buf)))
        return std::string(buf)+"\\APBConfigTool";
    const char* up=getenv("USERPROFILE");
    if(up) return std::string(up)+"\\Documents\\APBConfigTool";
    return ".\\APBConfigTool";
}

static bool LoadResourceBytes(int resourceId, std::vector<unsigned char>& out){
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if(!resource) return false;

    HGLOBAL loaded = LoadResource(nullptr, resource);
    if(!loaded) return false;

    DWORD size = SizeofResource(nullptr, resource);
    if(size == 0) return false;

    const void* data = LockResource(loaded);
    if(!data) return false;

    const auto* bytes = static_cast<const unsigned char*>(data);
    out.assign(bytes, bytes + size);
    return true;
}

struct PreviewFontSpec {
    int resourceId;
    const char* tag;
    float pixelSize;
};

struct PreviewFontEntry {
    ImFont* font = nullptr;
    float pixelSize = 0.f;
};

static std::map<std::string, PreviewFontEntry> g_previewFonts;
static std::vector<std::vector<unsigned char>> g_previewFontBlobs;

static void RegisterPreviewFonts(ImGuiIO& io){
    g_previewFonts.clear();
    g_previewFontBlobs.clear();

    const PreviewFontSpec specs[] = {
        {IDR_FONT_HELVETICA_REGULAR, "APBMenus_Font.APB_Helvetica_Regular_11", 11.f},
        {IDR_FONT_HELVETICA_REGULAR, "APBMenus_Font.APB_Helvetica_Regular_12", 12.f},
        {IDR_FONT_HELVETICA_REGULAR, "APBMenus_Font.APB_Helvetica_Regular_14", 14.f},
        {IDR_FONT_HELVETICA_REGULAR, "APBMenus_Font.APB_Helvetica_Regular_16", 16.f},
        {IDR_FONT_HELVETICA_BOLD,    "APBMenus_Font.APB_Helvetica_Bold_11",    11.f},
        {IDR_FONT_HELVETICA_BOLD,    "APBMenus_Font.APB_Helvetica_Bold_13",    13.f},
        {IDR_FONT_HELVETICA_BOLD,    "APBMenus_Font.APB_Helvetica_Bold_14",    14.f},
        {IDR_FONT_HELVETICA_BOLD,    "APBMenus_Font.APB_Helvetica_Bold_24",    24.f},
    };

    std::map<int, size_t> blobIndexByResource;
    auto getBlob = [&](int resourceId) -> std::vector<unsigned char>* {
        auto it = blobIndexByResource.find(resourceId);
        if(it != blobIndexByResource.end())
            return &g_previewFontBlobs[it->second];

        std::vector<unsigned char> data;
        if(!LoadResourceBytes(resourceId, data) || data.empty())
            return nullptr;

        const size_t index = g_previewFontBlobs.size();
        g_previewFontBlobs.push_back(std::move(data));
        blobIndexByResource[resourceId] = index;
        return &g_previewFontBlobs[index];
    };

    for(const auto& spec : specs){
        std::vector<unsigned char>* blob = getBlob(spec.resourceId);
        if(!blob) continue;

        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        ImFont* font = io.Fonts->AddFontFromMemoryTTF(blob->data(), (int)blob->size(), spec.pixelSize, &cfg);
        if(font)
            g_previewFonts[spec.tag] = {font, spec.pixelSize};
    }
}

// ── Create all required Documents\APBConfigTool\ subfolders on launch ─────
static void InitDocumentsFolder(){
    namespace fs = std::filesystem;
    std::string base = GetAppDocumentsDir();
    fs::create_directories(base);
    fs::create_directories(base+"\\Themes");
    fs::create_directories(base+"\\Presets");
}

namespace apb::gui {
ImFont* ResolvePreviewFont(const char* apbFontTag, float* outPixelSize){
    if(outPixelSize) *outPixelSize = 0.f;
    if(!apbFontTag || !*apbFontTag) return nullptr;

    auto it = g_previewFonts.find(apbFontTag);
    if(it == g_previewFonts.end())
        return nullptr;

    if(outPixelSize) *outPixelSize = it->second.pixelSize;
    return it->second.font;
}
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
static LRESULT WINAPI WndProc(HWND hw,UINT msg,WPARAM wp,LPARAM lp){
    if(ImGui_ImplWin32_WndProcHandler(hw,msg,wp,lp)) return true;
    switch(msg){
    case WM_SIZE:
        if(g_dev&&wp!=SIZE_MINIMIZED){
            DropRTV();
            g_sc->ResizeBuffers(0,LOWORD(lp),HIWORD(lp),DXGI_FORMAT_UNKNOWN,0);
            CreateRTV();
        } return 0;
    case WM_SYSCOMMAND: if((wp&0xfff0)==SC_KEYMENU) return 0; break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hw,msg,wp,lp);
}

int WINAPI WinMain(HINSTANCE hi,HINSTANCE,LPSTR,int){
    // Init COM (required for WIC image loading and shell functions)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Create Documents\APBConfigTool\ structure immediately on launch
    InitDocumentsFolder();

    WNDCLASSEXW wc={sizeof(wc),CS_CLASSDC,WndProc,0,0,hi,
        nullptr,nullptr,nullptr,nullptr,L"APBConfigTool",nullptr};
    RegisterClassExW(&wc);
    HWND hw=CreateWindowW(wc.lpszClassName,L"APB Config Tool",
        WS_OVERLAPPEDWINDOW,100,100,1280,800,nullptr,nullptr,hi,nullptr);

    if(!InitDX(hw)){ DestroyWindow(hw); UnregisterClassW(wc.lpszClassName,hi); CoUninitialize(); return 1; }

    ShowWindow(hw,SW_SHOWDEFAULT); UpdateWindow(hw);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "apbtool.ini";
    {
        static std::vector<unsigned char> uiFontData;
        if(LoadResourceBytes(IDR_FONT_BENTONSANS_REGULAR, uiFontData) && !uiFontData.empty()){
            ImFontConfig cfg;
            cfg.FontDataOwnedByAtlas = false;
            if(io.Fonts->AddFontFromMemoryTTF(uiFontData.data(), (int)uiFontData.size(), 16.0f, &cfg) == nullptr)
                io.Fonts->AddFontDefault();
        } else {
            io.Fonts->AddFontDefault();
        }
    }
    RegisterPreviewFonts(io);
    ImGui_ImplWin32_Init(hw);
    ImGui_ImplDX11_Init(g_dev,g_ctx);

    const float cc[4]={0.043f,0.043f,0.047f,1.f};
    bool done=false;
    while(!done){
        MSG msg;
        while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){
            TranslateMessage(&msg); DispatchMessageW(&msg);
            if(msg.message==WM_QUIT) done=true;
        }
        if(done) break;
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        apb::gui::Render();
        ImGui::Render();
        g_ctx->OMSetRenderTargets(1,&g_rtv,nullptr);
        g_ctx->ClearRenderTargetView(g_rtv,cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_sc->Present(1,0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    ShutDX();
    DestroyWindow(hw);
    UnregisterClassW(wc.lpszClassName,hi);
    CoUninitialize();
    return 0;
}
