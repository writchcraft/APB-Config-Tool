#pragma once
// ImageLoader.h – load PNG/JPG to ID3D11ShaderResourceView using Win32 WIC
// Zero external dependencies – WIC ships with every Windows Vista+ install.
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincodec.h>          // WIC
#include <d3d11.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#include <string>
#include <vector>

namespace apb {

inline bool CreateTextureFromWicDecoder(
    ID3D11Device*              device,
    IWICImagingFactory*        wic,
    IWICBitmapDecoder*         decoder,
    ID3D11ShaderResourceView** outSRV,
    int*                       outW,
    int*                       outH)
{
    if(!device || !wic || !decoder || !outSRV) return false;

    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;

    auto cleanup = [&]{
        if(conv) conv->Release();
        if(frame) frame->Release();
    };

    if(FAILED(decoder->GetFrame(0, &frame))){ cleanup(); return false; }
    if(FAILED(wic->CreateFormatConverter(&conv))){ cleanup(); return false; }
    if(FAILED(conv->Initialize(frame,
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0,
        WICBitmapPaletteTypeCustom))){ cleanup(); return false; }

    UINT w = 0, h = 0;
    conv->GetSize(&w, &h);
    if(w == 0 || h == 0){ cleanup(); return false; }

    UINT stride = w * 4;
    UINT bufSize = stride * h;
    std::vector<BYTE> pixels(bufSize);
    if(FAILED(conv->CopyPixels(nullptr, stride, bufSize, pixels.data()))){ cleanup(); return false; }

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = pixels.data();
    sd.SysMemPitch = stride;

    ID3D11Texture2D* tex = nullptr;
    if(FAILED(device->CreateTexture2D(&td, &sd, &tex))){ cleanup(); return false; }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
    srvd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(tex, &srvd, outSRV);
    tex->Release();

    if(outW) *outW = (int)w;
    if(outH) *outH = (int)h;

    cleanup();
    return *outSRV != nullptr;
}

// Loads a PNG/JPG from disk into a DX11 shader resource view (RGBA8).
// Returns true on success. outW/outH are set to the image dimensions.
inline bool LoadTextureFromFile(
    ID3D11Device*              device,
    const std::string&         path,
    ID3D11ShaderResourceView** outSRV,
    int*                       outW,
    int*                       outH)
{
    if(!device || !outSRV) return false;
    *outSRV = nullptr;

    // ── Init WIC factory ──────────────────────────────────────────────
    IWICImagingFactory* wic = nullptr;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;

    // ── Decode ────────────────────────────────────────────────────────
    // Convert path to wide string
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wpath(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);

    IWICBitmapDecoder* decoder = nullptr;

    auto cleanup = [&]{
        if(decoder) decoder->Release();
        wic->Release();
    };

    if(FAILED(wic->CreateDecoderFromFilename(wpath.c_str(), nullptr,
        GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder))){ cleanup(); return false; }

    bool ok = CreateTextureFromWicDecoder(device, wic, decoder, outSRV, outW, outH);
    cleanup();
    return ok;
}

inline bool LoadTextureFromResource(
    ID3D11Device*              device,
    int                        resourceId,
    ID3D11ShaderResourceView** outSRV,
    int*                       outW,
    int*                       outH)
{
    if(!device || !outSRV) return false;
    *outSRV = nullptr;

    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if(!resource) return false;
    HGLOBAL loaded = LoadResource(nullptr, resource);
    if(!loaded) return false;
    DWORD size = SizeofResource(nullptr, resource);
    if(size == 0) return false;
    void* data = LockResource(loaded);
    if(!data) return false;

    IWICImagingFactory* wic = nullptr;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;

    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;

    auto cleanup = [&]{
        if(decoder) decoder->Release();
        if(stream) stream->Release();
        wic->Release();
    };

    if(FAILED(wic->CreateStream(&stream))){ cleanup(); return false; }
    if(FAILED(stream->InitializeFromMemory(static_cast<WICInProcPointer>(data), size))){ cleanup(); return false; }
    if(FAILED(wic->CreateDecoderFromStream(stream, nullptr,
        WICDecodeMetadataCacheOnLoad, &decoder))){ cleanup(); return false; }

    bool ok = CreateTextureFromWicDecoder(device, wic, decoder, outSRV, outW, outH);
    cleanup();
    return ok;
}

// Convenience: release a texture safely
inline void ReleaseTexture(ID3D11ShaderResourceView*& srv){
    if(srv){ srv->Release(); srv = nullptr; }
}

} // namespace apb
