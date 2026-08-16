#include <windows.h>
#include <mfapi.h>
#include "app_controller.h"

static std::wstring SanitizeUrlArg(const std::wstring& raw) {
    std::wstring s = raw;
    // Trim leading / trailing spaces and quotes
    size_t start = s.find_first_not_of(L" \t\r\n\"'");
    if (start == std::wstring::npos) return L"";
    size_t end = s.find_last_not_of(L" \t\r\n\"'");
    s = s.substr(start, end - start + 1);

    // If starts with --url=, remove prefix
    if (s.rfind(L"--url=", 0) == 0) {
        s = s.substr(6);
    } else if (s.rfind(L"--url ", 0) == 0) {
        s = s.substr(6);
    }

    // Re-trim quotes from URL
    start = s.find_first_not_of(L" \t\r\n\"'");
    if (start != std::wstring::npos) {
        end = s.find_last_not_of(L" \t\r\n\"'");
        s = s.substr(start, end - start + 1);
    }
    return s;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(nCmdShow);

    // Initialize COM and Media Foundation
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) return 1;

    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        CoUninitialize();
        return 1;
    }

    std::wstring baseUrl = L"http://127.0.0.1:8787";
    if (pCmdLine && wcslen(pCmdLine) > 0) {
        std::wstring sanitized = SanitizeUrlArg(pCmdLine);
        if (!sanitized.empty()) {
            baseUrl = sanitized;
        }
    }

    {
        km::app::AppController controller;
        if (controller.Initialize(hInstance, baseUrl)) {
            controller.RunMessageLoop();
        }
        controller.Shutdown();
    }

    MFShutdown();
    CoUninitialize();
    return 0;
}
