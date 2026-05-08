#include "backend/HttpClient.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#include <vector>

namespace apb {

static std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    if (!w.empty() && w.back() == 0) w.pop_back();
    return w;
}

HttpResponse httpGet(const std::string& urlStr, int timeoutMs, bool followRedirects) {
    HttpResponse res;
    std::wstring wurl = toWide(urlStr);

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t scheme[32]={}, host[256]={}, path[4096]={};
    uc.lpszScheme   = scheme; uc.dwSchemeLength   = 32;
    uc.lpszHostName = host;   uc.dwHostNameLength = 256;
    uc.lpszUrlPath  = path;   uc.dwUrlPathLength  = 4096;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return res;

    bool isHttps = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    HINTERNET hSes = WinHttpOpen(L"APBTool/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSes) return res;

    DWORD t = (DWORD)timeoutMs;
    WinHttpSetTimeouts(hSes, t, t, t, t);

    HINTERNET hCon = WinHttpConnect(hSes, host, uc.nPort, 0);
    if (!hCon) { WinHttpCloseHandle(hSes); return res; }

    DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hCon, L"GET", path, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) { WinHttpCloseHandle(hCon); WinHttpCloseHandle(hSes); return res; }

    if (followRedirects) {
        DWORD r = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(hReq, WINHTTP_OPTION_REDIRECT_POLICY, &r, sizeof(r));
    }

    if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hReq, nullptr))
    {
        DWORD code = 0, sz = sizeof(DWORD);
        WinHttpQueryHeaders(hReq,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
        res.statusCode = (int)code;

        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
            std::vector<char> buf(avail);
            DWORD rd = 0;
            WinHttpReadData(hReq, buf.data(), avail, &rd);
            res.body.append(buf.data(), rd);
        }
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hCon);
    WinHttpCloseHandle(hSes);
    return res;
}

} // namespace apb
