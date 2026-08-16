#include "main_window.h"

namespace km::ui {

constexpr int ID_AUDIO_COMBO = 1001;
constexpr int ID_VCAM_BTN = 1002;
constexpr int ID_NEW_SESSION_BTN = 1003;

MainWindow::MainWindow() = default;

MainWindow::~MainWindow() {
    if (hWnd_) {
        DestroyWindow(hWnd_);
        hWnd_ = nullptr;
    }
}

bool MainWindow::Create(HINSTANCE hInstance, int width, int height) {
    if (!hInstance) {
        hInstance = GetModuleHandleW(nullptr);
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &MainWindow::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"KMVirtualCameraReceiverMainClass";

    RegisterClassExW(&wc);

    WNDCLASSEXW wcChild{};
    wcChild.cbSize = sizeof(WNDCLASSEXW);
    wcChild.style = CS_HREDRAW | CS_VREDRAW;
    wcChild.lpfnWndProc = DefWindowProcW;
    wcChild.hInstance = hInstance;
    wcChild.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcChild.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcChild.lpszClassName = L"KMVirtualCameraPreviewClass";
    RegisterClassExW(&wcChild);

    // Compute explicit centered window rect
    RECT rc = { 0, 0, width, height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int realW = rc.right - rc.left;
    int realH = rc.bottom - rc.top;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - realW) / 2;
    int posY = (screenH - realH) / 2;
    if (posX < 50) posX = 50;
    if (posY < 50) posY = 50;

    hWnd_ = CreateWindowExW(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        L"KM Virtual Camera - Windows Receiver",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        posX, posY, realW, realH,
        nullptr, nullptr, hInstance, this
    );

    if (!hWnd_) return false;

    // Create Left Panel UI controls
    // QR Code Area: (20, 15, 300, 290)
    // Join URL Edit control: (20, 315, 300, 24)
    hUrlEdit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
        20, 315, 300, 24,
        hWnd_, nullptr, hInstance, nullptr
    );

    // Status Label: (20, 345, 300, 30)
    hStatusLabel_ = CreateWindowExW(
        0, L"STATIC", L"初期化中...",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 345, 300, 30,
        hWnd_, nullptr, hInstance, nullptr
    );

    // Audio Output Label & Dropdown
    CreateWindowExW(0, L"STATIC", L"音声出力先 (VB-CABLE CABLE Input):", WS_CHILD | WS_VISIBLE, 20, 385, 300, 20, hWnd_, nullptr, hInstance, nullptr);
    hAudioCombo_ = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        20, 410, 300, 150,
        hWnd_, (HMENU)(INT_PTR)ID_AUDIO_COMBO, hInstance, nullptr
    );

    // Virtual Camera toggle button
    hVcamButton_ = CreateWindowExW(
        0, L"BUTTON", L"仮想カメラ開始",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        20, 455, 145, 36,
        hWnd_, (HMENU)(INT_PTR)ID_VCAM_BTN, hInstance, nullptr
    );

    // New Session button
    hNewSessionBtn_ = CreateWindowExW(
        0, L"BUTTON", L"新しいセッション",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        175, 455, 145, 36,
        hWnd_, (HMENU)(INT_PTR)ID_NEW_SESSION_BTN, hInstance, nullptr
    );

    // Create Right Panel: D3D11 Video Preview window (340, 15, 720, 540)
    hPreviewWnd_ = CreateWindowExW(
        0, wcChild.lpszClassName, L"",
        WS_CHILD | WS_VISIBLE,
        340, 15, 720, 540,
        hWnd_, nullptr, hInstance, nullptr
    );

    d3dPreview_.Initialize(hPreviewWnd_, 720, 540);
    return true;
}

void MainWindow::Show(int nCmdShow) {
    if (hWnd_) {
        ShowWindow(hWnd_, nCmdShow);
        UpdateWindow(hWnd_);
        SetForegroundWindow(hWnd_);
    }
}

void MainWindow::SetJoinUrl(const std::string& joinUrl) {
    qrView_.SetText(joinUrl);
    if (hUrlEdit_) {
        std::wstring wUrl(joinUrl.begin(), joinUrl.end());
        SetWindowTextW(hUrlEdit_, wUrl.c_str());
    }
    InvalidateRect(hWnd_, nullptr, TRUE);
    UpdateWindow(hWnd_);
}

void MainWindow::SetStatusText(const std::wstring& status) {
    if (hStatusLabel_) {
        SetWindowTextW(hStatusLabel_, status.c_str());
    }
}

void MainWindow::SetAudioDevices(const std::vector<audio::AudioDevice>& devices, int selectedIndex) {
    if (!hAudioCombo_) return;
    SendMessageW(hAudioCombo_, CB_RESETCONTENT, 0, 0);

    for (const auto& d : devices) {
        std::wstring label = d.friendlyName;
        if (d.isCableInput) label += L" [VB-CABLE 推奨]";
        SendMessageW(hAudioCombo_, CB_ADDSTRING, 0, (LPARAM)label.c_str());
    }

    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(devices.size())) {
        SendMessageW(hAudioCombo_, CB_SETCURSEL, selectedIndex, 0);
    }
}

void MainWindow::SetVirtualCameraStatus(bool isStarted) {
    if (hVcamButton_) {
        SetWindowTextW(hVcamButton_, isStarted ? L"仮想カメラ停止" : L"仮想カメラ開始");
    }
}

void MainWindow::RenderPreviewFrame(std::span<const uint8_t> nv12Data) {
    d3dPreview_.RenderNv12Frame(nv12Data, 1280, 720);
}

LRESULT CALLBACK MainWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<MainWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->hWnd_ = hWnd;
    } else {
        pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->HandleMessage(hWnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            // Draw QR code in left area: (20, 20, 320, 320)
            qrView_.Draw(hdc, 20, 20, 320, 320);

            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            int wmEvent = HIWORD(wParam);

            if (wmId == ID_AUDIO_COMBO && wmEvent == CBN_SELCHANGE) {
                int curSel = static_cast<int>(SendMessageW(hAudioCombo_, CB_GETCURSEL, 0, 0));
                if (onAudioDeviceChanged_) {
                    onAudioDeviceChanged_(curSel);
                }
            } else if (wmId == ID_VCAM_BTN && wmEvent == BN_CLICKED) {
                if (onToggleVirtualCamera_) {
                    onToggleVirtualCamera_();
                }
            } else if (wmId == ID_NEW_SESSION_BTN && wmEvent == BN_CLICKED) {
                if (onNewSession_) {
                    onNewSession_();
                }
            }
            return 0;
        }

        case WM_SIZE: {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            if (hPreviewWnd_) {
                int previewX = 360;
                int previewY = 20;
                int previewW = (std::max)(100, w - previewX - 20);
                int previewH = (std::max)(100, h - previewY - 20);
                MoveWindow(hPreviewWnd_, previewX, previewY, previewW, previewH, TRUE);
                d3dPreview_.Resize(previewW, previewH);
            }
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

} // namespace km::ui
