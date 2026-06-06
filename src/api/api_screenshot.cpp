#include "api_screenshot.h"
#include "../util/image_utils.h"

#include <sstream>
#include <vector>
#include <filesystem>

ScreenshotAPI::ScreenshotAPI() {}

ScreenshotAPI::~ScreenshotAPI() {}

std::wstring ScreenshotAPI::utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring wstr(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, wstr.data(), len);
    wstr.pop_back();
    return wstr;
}

std::string ScreenshotAPI::httpGet(const std::string& url) {
    std::wstring wurl(utf8ToWide(url));

    HINTERNET hSession = WinHttpOpen(L"TileMapEditor/Screenshot/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256] = {};
    wchar_t urlPath[4096] = {};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = 4096;

    std::vector<wchar_t> wurlVec(wurl.begin(), wurl.end());
    wurlVec.push_back(L'\0');

    if (!WinHttpCrackUrl(wurlVec.data(), 0, 0, &urlComp)) {
        WinHttpCloseHandle(hSession);
        return "";
    }

    HINTERNET hConnect = WinHttpConnect(hSession, urlComp.lpszHostName,
                                        urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return "";
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlComp.lpszUrlPath,
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    if (flags & WINHTTP_FLAG_SECURE) {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    std::string response;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
            response.append(buffer.data(), bytesRead);
        }
        bytesAvailable = 0;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return response;
}

bool ScreenshotAPI::captureWindow(const char* windowName, Gdiplus::Bitmap** outBmp) {
    std::string encodedName(windowName);

    std::ostringstream oss;
    oss << "http://127.0.0.1:8766/api/screenshot?window_name=" << encodedName;

    std::string imageData = httpGet(oss.str());
    if (imageData.empty()) return false;

    std::string tmpPath = std::filesystem::temp_directory_path().string() +
                          "\\tilemap_screenshot_" + std::to_string(GetCurrentProcessId()) + ".png";

    FILE* f = nullptr;
    fopen_s(&f, tmpPath.c_str(), "wb");
    if (!f) return false;
    fwrite(imageData.data(), 1, imageData.size(), f);
    fclose(f);

    std::wstring wtmp(tmpPath.begin(), tmpPath.end());
    *outBmp = loadBmpFromFile(wtmp.c_str());

    std::filesystem::remove(tmpPath);

    return *outBmp != nullptr;
}

bool ScreenshotAPI::verifyEditor(const char* expectedTile, Gdiplus::Bitmap** outBmp) {
    if (!captureWindow("TileMapEditor", outBmp)) {
        return false;
    }

    if (!*outBmp) return false;

    int w = static_cast<int>((*outBmp)->GetWidth());
    int h = static_cast<int>((*outBmp)->GetHeight());

    if (w == 0 || h == 0) return false;

    int sampleX = w / 2;
    int sampleY = h / 2;

    Gdiplus::Color centerColor;
    (*outBmp)->GetPixel(sampleX, sampleY, &centerColor);

    bool hasContent = (centerColor.GetR() != 0 || centerColor.GetG() != 0 ||
                       centerColor.GetB() != 0);

    return hasContent;
}
