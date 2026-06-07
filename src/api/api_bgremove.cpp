#include "api_bgremove.h"

#include <sstream>
#include <vector>

BgRemoveAPI::BgRemoveAPI() {}

BgRemoveAPI::~BgRemoveAPI() {}

std::string BgRemoveAPI::readFile(const char* path) {
    FILE* f = nullptr;
    fopen_s(&f, path, "rb");
    if (!f) return "";

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::string data(size, '\0');
    fread(data.data(), 1, size, f);
    fclose(f);
    return data;
}

bool BgRemoveAPI::writeFile(const char* path, const std::string& data) {
    FILE* f = nullptr;
    fopen_s(&f, path, "wb");
    if (!f) return false;
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return true;
}

std::string BgRemoveAPI::buildMultipartBody(const std::string& boundary,
                                              const std::string& fileData,
                                              const std::string& fileName) {
    std::ostringstream body;

    body << "--" << boundary << "\r\n";
    body << "Content-Disposition: form-data; name=\"image\"; filename=\"" << fileName << "\"\r\n";
    body << "Content-Type: image/png\r\n\r\n";
    body << fileData;
    body << "\r\n--" << boundary << "--\r\n";

    return body.str();
}

std::string BgRemoveAPI::httpPost(const std::string& host, int port,
                                    const std::string& path,
                                    const std::string& body,
                                    const std::string& contentType) {
    std::wstring whost(host.begin(), host.end());
    std::wstring wpath(path.begin(), path.end());

    HINTERNET hSession = WinHttpOpen(L"TileMapEditor/BgRemove/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return "";
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", wpath.c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    std::wstring wContentType(contentType.begin(), contentType.end());
    BOOL sent = WinHttpSendRequest(hRequest,
                                   (L"Content-Type: " + wContentType).c_str(),
                                   static_cast<DWORD>(-1),
                                   const_cast<char*>(body.data()),
                                   static_cast<DWORD>(body.size()),
                                   static_cast<DWORD>(body.size()), 0);

    if (!sent || !WinHttpReceiveResponse(hRequest, nullptr)) {
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

bool BgRemoveAPI::removeBackground(const char* inputPath, char* outputPath, int outputSize) {
    std::string fileData = readFile(inputPath);
    if (fileData.empty()) return false;

    std::string boundary = "----TileMapEditorBoundary";
    std::string body = buildMultipartBody(boundary, fileData, "image.png");
    std::string contentType = "multipart/form-data; boundary=" + boundary;

    std::string response = httpPost("127.0.0.1", 5000, "/", body, contentType);
    if (response.empty()) return false;

    size_t headerEnd = response.find("\r\n\r\n");
    std::string pngData;
    if (headerEnd != std::string::npos) {
        pngData = response.substr(headerEnd + 4);
    } else {
        pngData = response;
    }

    if (!writeFile(outputPath, pngData)) return false;
    return true;
}
