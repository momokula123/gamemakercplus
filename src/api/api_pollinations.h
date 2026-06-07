#pragma once

#include <windows.h>
#include <winhttp.h>
#include <propkeydef.h>
#include <gdiplus.h>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "gdiplus.lib")

class GUIState;

class PollinationsAPI {
public:
    PollinationsAPI();
    ~PollinationsAPI();

    std::string generateImage(const char* prompt, int width = 512, int height = 512);
    bool generateTileTexture(const char* prompt, Gdiplus::Bitmap** outBmp, int tileSize = 32);

    void generateAsync(const char* prompt, int width, int height, GUIState* state);
    bool isGenerating() const;
    std::string getLastResult() const;
    void clearResult();

private:
    std::string urlEncode(const std::string& str);
    std::string downloadUrl(const std::string& url, GUIState* state);
    bool downloadToFile(const std::string& url, const std::string& outPath, GUIState* state);

    std::atomic<bool> m_isGenerating{ false };
    std::atomic<bool> m_shouldCancel{ false };
    std::string m_lastResult;
    std::thread m_worker;
    std::mutex m_mutex;
};
