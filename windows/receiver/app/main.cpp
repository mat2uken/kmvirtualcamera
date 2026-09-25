#include <windows.h>
#include <mfapi.h>
#include <io.h>
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

    // Redirect stdout/stderr to receiver_debug.log with shared-read access (_SH_DENYNO)
    FILE* fpLog = _wfsopen(L"receiver_debug.log", L"a", _SH_DENYNO);
    if (fpLog) {
        setvbuf(fpLog, nullptr, _IONBF, 0);
        int fd = _fileno(fpLog);
        _dup2(fd, _fileno(stdout));
        _dup2(fd, _fileno(stderr));
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
    }
    std::ios::sync_with_stdio(true);
    std::cout << "\n=== KM Virtual Camera Receiver Started ===" << std::endl;

    // Initialize COM and Media Foundation
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) { std::cerr << "[Main] CoInitializeEx failed hr=0x" << std::hex << unsigned(hr) << std::dec << "\n"; return 1; }

    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        std::cerr << "[Main] MFStartup failed hr=0x" << std::hex << unsigned(hr) << std::dec << "\n";
        CoUninitialize();
        return 1;
    }

    std::wstring baseUrl = L"https://webrtc-bridge-signaling.mat2uken.workers.dev";
    if (pCmdLine && wcslen(pCmdLine) > 0) {
        std::wstring sanitized = SanitizeUrlArg(pCmdLine);
        if (!sanitized.empty()) {
            baseUrl = sanitized;
        }
    }

    try {
        km::app::AppController controller;
        if (controller.Initialize(hInstance, baseUrl)) {
            std::cout << "[Main] Initialized successfully, entering RunMessageLoop..." << std::endl;
            controller.RunMessageLoop();
            std::cout << "[Main] Exited RunMessageLoop." << std::endl;
        } else {
            std::cerr << "[Main] ERROR: controller.Initialize failed!" << std::endl;
        }
        controller.Shutdown();
    } catch (const std::exception& e) {
        std::cerr << "[Main] FATAL EXCEPTION: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[Main] FATAL UNKNOWN EXCEPTION!" << std::endl;
    }

    MFShutdown();
    CoUninitialize();
    return 0;
}
