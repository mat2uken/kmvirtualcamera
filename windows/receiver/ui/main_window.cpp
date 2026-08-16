#include "main_window.h"

namespace km::ui {

constexpr int ID_AUDIO_COMBO = 1001;
constexpr int ID_VCAM_BTN = 1002;
constexpr int ID_NEW_SESSION_BTN = 1003;
constexpr int ID_ROTATION_COMBO = 1004;
constexpr int ID_ROT_LEFT_BTN = 1005;
constexpr int ID_ROT_RIGHT_BTN = 1006;

MainWindow::MainWindow() = default;

MainWindow::~MainWindow() {
    if (hFont_) {
        DeleteObject(hFont_);
        hFont_ = nullptr;
    }
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

    // Create modern clean font (Segoe UI / Meiryo UI)
    hFont_ = CreateFontW(
        -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    if (!hFont_) {
        hFont_ = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }

    // Create Left Panel UI controls
    // QR Code Area: (25, 20, 270, 270)
    // Join URL Edit control: (25, 300, 270, 26)
    hUrlEdit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
        25, 300, 270, 26,
        hWnd_, nullptr, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hUrlEdit_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    // Status Label: (25, 335, 270, 42)
    hStatusLabel_ = CreateWindowExW(
        0, L"STATIC", L"初期化中...",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        25, 335, 270, 42,
        hWnd_, nullptr, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hStatusLabel_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    // Audio Output Label & Dropdown
    hAudioLabel_ = CreateWindowExW(0, L"STATIC", L"音声出力先 (VB-CABLE CABLE Input):", WS_CHILD | WS_VISIBLE, 25, 385, 270, 18, hWnd_, nullptr, hInstance, nullptr);
    if (hFont_) SendMessageW(hAudioLabel_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hAudioCombo_ = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        25, 406, 270, 150,
        hWnd_, (HMENU)(INT_PTR)ID_AUDIO_COMBO, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hAudioCombo_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    // Rotation Label, Dropdown & Quick Buttons
    hRotationLabel_ = CreateWindowExW(0, L"STATIC", L"映像回転 (Receiver Rotation):", WS_CHILD | WS_VISIBLE, 25, 438, 270, 18, hWnd_, nullptr, hInstance, nullptr);
    if (hFont_) SendMessageW(hRotationLabel_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hRotationCombo_ = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        25, 458, 140, 150,
        hWnd_, (HMENU)(INT_PTR)ID_ROTATION_COMBO, hInstance, nullptr
    );
    if (hFont_) {
        SendMessageW(hRotationCombo_, WM_SETFONT, (WPARAM)hFont_, TRUE);
    }
    SendMessageW(hRotationCombo_, CB_ADDSTRING, 0, (LPARAM)L"0° (通常)");
    SendMessageW(hRotationCombo_, CB_ADDSTRING, 0, (LPARAM)L"90° (時計回り)");
    SendMessageW(hRotationCombo_, CB_ADDSTRING, 0, (LPARAM)L"180° (上下反転)");
    SendMessageW(hRotationCombo_, CB_ADDSTRING, 0, (LPARAM)L"270° (反時計回り)");
    SendMessageW(hRotationCombo_, CB_SETCURSEL, 0, 0);

    hRotLeftBtn_ = CreateWindowExW(
        0, L"BUTTON", L"↺ 左90°",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        170, 458, 60, 26,
        hWnd_, (HMENU)(INT_PTR)ID_ROT_LEFT_BTN, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hRotLeftBtn_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hRotRightBtn_ = CreateWindowExW(
        0, L"BUTTON", L"↻ 右90°",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        235, 458, 60, 26,
        hWnd_, (HMENU)(INT_PTR)ID_ROT_RIGHT_BTN, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hRotRightBtn_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    // Virtual Camera toggle button
    hVcamButton_ = CreateWindowExW(
        0, L"BUTTON", L"仮想カメラ開始",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        25, 502, 130, 36,
        hWnd_, (HMENU)(INT_PTR)ID_VCAM_BTN, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hVcamButton_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    // New Session button
    hNewSessionBtn_ = CreateWindowExW(
        0, L"BUTTON", L"新しいセッション",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        165, 502, 130, 36,
        hWnd_, (HMENU)(INT_PTR)ID_NEW_SESSION_BTN, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hNewSessionBtn_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    // Create Right Panel: D3D11 Video Preview window (315, 20, 755, 550)
    hPreviewWnd_ = CreateWindowExW(
        0, wcChild.lpszClassName, L"",
        WS_CHILD | WS_VISIBLE,
        315, 20, 755, 550,
        hWnd_, nullptr, hInstance, nullptr
    );

    d3dPreview_.Initialize(hPreviewWnd_, 755, 550);
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

            // Draw QR code in left area: (25, 20, 270, 270)
            qrView_.Draw(hdc, 25, 20, 270, 270);

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
            } else if (wmId == ID_ROTATION_COMBO && wmEvent == CBN_SELCHANGE) {
                int curSel = static_cast<int>(SendMessageW(hRotationCombo_, CB_GETCURSEL, 0, 0));
                if (onRotationChanged_ && curSel >= 0) {
                    onRotationChanged_(curSel * 90);
                }
            } else if (wmId == ID_ROT_LEFT_BTN && wmEvent == BN_CLICKED) {
                int curSel = static_cast<int>(SendMessageW(hRotationCombo_, CB_GETCURSEL, 0, 0));
                if (curSel < 0) curSel = 0;
                curSel = (curSel + 3) % 4; // 270° counter-clockwise
                SendMessageW(hRotationCombo_, CB_SETCURSEL, curSel, 0);
                if (onRotationChanged_) {
                    onRotationChanged_(curSel * 90);
                }
            } else if (wmId == ID_ROT_RIGHT_BTN && wmEvent == BN_CLICKED) {
                int curSel = static_cast<int>(SendMessageW(hRotationCombo_, CB_GETCURSEL, 0, 0));
                if (curSel < 0) curSel = 0;
                curSel = (curSel + 1) % 4; // 90° clockwise
                SendMessageW(hRotationCombo_, CB_SETCURSEL, curSel, 0);
                if (onRotationChanged_) {
                    onRotationChanged_(curSel * 90);
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
