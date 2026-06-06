#pragma once

#include <windows.h>
#include <winhttp.h>
#include <string>

#pragma comment(lib, "winhttp.lib")

class BgRemoveAPI {
public:
    BgRemoveAPI();
    ~BgRemoveAPI();

    bool removeBackground(const char* inputPath, char* outputPath, int outputSize);

private:
    std::string readFile(const char* path);
    bool writeFile(const char* path, const std::string& data);
    std::string buildMultipartBody(const std::string& boundary,
                                    const std::string& fileData,
                                    const std::string& fileName);
    std::string httpPost(const std::string& host, int port,
                         const std::string& path,
                         const std::string& body,
                         const std::string& contentType);
};
