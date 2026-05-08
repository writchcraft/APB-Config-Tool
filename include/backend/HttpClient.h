#pragma once
// Synchronous HTTP GET — pure WinHTTP, no Qt dependencies in this header.
// The .cpp uses WinHTTP directly so this header can be included by any TU.
#include <string>

namespace apb {

struct HttpResponse {
    int         statusCode = 0;
    std::string body;                           // raw UTF-8 / binary bytes
    bool        ok() const { return statusCode >= 200 && statusCode < 300; }
};

// Blocking GET.  Safe to call from any thread (no Qt event loop needed).
HttpResponse httpGet(const std::string& url, int timeoutMs = 12000,
                     bool followRedirects = true);

} // namespace apb
