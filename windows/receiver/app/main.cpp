#include <windows.h>
#include <mfapi.h>
#include "app_controller.h"

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
        // Simple command line parsing for URL
        std::wstring cmd = pCmdLine;
        size_t pos = cmd.find(L"--url=");
        if (pos != std::wstring::npos) {
            baseUrl = cmd.substr(pos + 6);
        } else if (cmd.find(L"http") == 0) {
            baseUrl = cmd;
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
