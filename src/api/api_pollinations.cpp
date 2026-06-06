#include "api_pollinations.h"
#include "../gui/gui.h"
#include "../util/image_utils.h"

#include <sstream>
#include <vector>
#include <iomanip>
#include <mutex>
#include <filesystem>

namespace fs = std::filesystem;

PollinationsAPI::PollinationsAPI() {}

PollinationsAPI::~PollinationsAPI() {
    m_shouldCancel = true;
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

std::string PollinationsAPI::urlEncode(const std::string& str) {
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex;

    for (unsigned char c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << static_cast<char>(c);
        } else if (c == ' ') {
            encoded << '+';
        } else {
            encoded << '%' << std::setw(2) << static_cast<int>(c);
        }
    }
    return encoded.str();
}

std::string PollinationsAPI::downloadUrl(const std::string& url, GUIState* state) {
    if (state) {
        strncpy_s(state->statusText, "正在连接 pollinations.ai ...", sizeof(state->statusText));
    }

    std::wstring wurl(url.begin(), url.end());
    HINTERNET hSession = WinHttpOpen(L"TileMapEditor/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256] = {};
    wchar_t urlPath[2048] = {};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = 2048;

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

    if (state) {
        strncpy_s(state->statusText, "正在下载AI生成图像...", sizeof(state->statusText));
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    std::string imageData;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        if (m_shouldCancel) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }

        std::vector<char> buffer(bytesAvailable);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
            imageData.append(buffer.data(), bytesRead);
        }
        bytesAvailable = 0;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (state) {
        strncpy_s(state->statusText, "下载完成", sizeof(state->statusText));
    }

    return imageData;
}

bool PollinationsAPI::downloadToFile(const std::string& url, const std::string& outPath, GUIState* state) {
    std::string data = downloadUrl(url, state);
    if (data.empty()) return false;

    FILE* f = nullptr;
    fopen_s(&f, outPath.c_str(), "wb");
    if (!f) return false;
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return true;
}

std::string PollinationsAPI::generateImage(const char* prompt, int width, int height) {
    std::string encoded = urlEncode(std::string(prompt));
    std::ostringstream oss;
    oss << "https://image.pollinations.ai/prompt/" << encoded
        << "?width=" << width << "&height=" << height << "&nologo=true";

    std::string tmpPath = fs::temp_directory_path().string() + "\\tilemap_gen_" +
                          std::to_string(GetCurrentProcessId()) + ".png";

    if (downloadToFile(oss.str(), tmpPath, nullptr)) {
        return tmpPath;
    }
    return "";
}

bool PollinationsAPI::generateTileTexture(const char* prompt, Gdiplus::Bitmap** outBmp, int tileSize) {
    std::string path = generateImage(prompt, tileSize, tileSize);
    if (path.empty()) return false;

    std::wstring wpath(path.begin(), path.end());
    *outBmp = loadBmpFromFile(wpath.c_str());

    if (!*outBmp) return false;

    if ((*outBmp)->GetWidth() != tileSize || (*outBmp)->GetHeight() != tileSize) {
        Gdiplus::Bitmap* resized = resizeBitmap(*outBmp, tileSize, tileSize);
        delete *outBmp;
        *outBmp = resized;
    }

    fs::remove(path);
    return *outBmp != nullptr;
}

void PollinationsAPI::generateAsync(const char* prompt, int width, int height, GUIState* state) {
    if (m_isGenerating) return;

    m_shouldCancel = false;
    m_isGenerating = true;
    m_lastResult.clear();

    std::string promptCopy = prompt;
    int w = width;
    int h = height;

    if (m_worker.joinable()) {
        m_worker.join();
    }

    m_worker = std::thread([this, promptCopy, w, h, state]() {
        std::string result = generateImage(promptCopy.c_str(), w, h);
        m_lastResult = result;
        m_isGenerating = false;
        if (state && !result.empty()) {
            strncpy_s(state->statusText, "AI图像生成完成", sizeof(state->statusText));
        } else if (state) {
            strncpy_s(state->statusText, "AI图像生成失败", sizeof(state->statusText));
        }
    });
}

bool PollinationsAPI::isGenerating() const {
    return m_isGenerating.load();
}

std::string PollinationsAPI::getLastResult() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
    return m_lastResult;
}

void PollinationsAPI::clearResult() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastResult.clear();
}
