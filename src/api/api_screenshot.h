#pragma once

#include <windows.h>
#include <winhttp.h>
#include <propkeydef.h>
#include <gdiplus.h>
#include <string>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "gdiplus.lib")

class ScreenshotAPI {
public:
    ScreenshotAPI();
    ~ScreenshotAPI();

    bool captureWindow(const char* windowName, Gdiplus::Bitmap** outBmp);
    bool verifyEditor(const char* expectedTile, Gdiplus::Bitmap** outBmp);

private:
    std::string httpGet(const std::string& url);
    std::wstring utf8ToWide(const std::string& str);
};
