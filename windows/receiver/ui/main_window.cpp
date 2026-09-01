#include "main_window.h"

namespace km::ui {

constexpr int ID_AUDIO_COMBO = 1001;
constexpr int ID_VCAM_BTN = 1002;
constexpr int ID_NEW_SESSION_BTN = 1003;
constexpr int ID_ROTATION_COMBO = 1004;
constexpr int ID_ROT_LEFT_BTN = 1005;
constexpr int ID_ROT_RIGHT_BTN = 1006;
constexpr int ID_REGISTER_VCAM_BTN = 1007;
constexpr int ID_CHECK_CAMERAS_BTN = 1008;
constexpr int ID_TEST_PATTERN_BTN = 1009;
constexpr int ID_TORCH_BTN = 1010;
constexpr int ID_SWITCH_CAM_BTN = 1011;
constexpr int ID_ZOOM_1X_BTN = 1012;
constexpr int ID_ZOOM_2X_BTN = 1013;
constexpr int ID_ZOOM_3X_BTN = 1014;

MainWindow::MainWindow() {
    InitializeSRWLock(&previewSrwLock_);
    hPreviewFrameEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

MainWindow::~MainWindow() {
    isPreviewWorkerRunning_ = false;
    if (hPreviewFrameEvent_) SetEvent(hPreviewFrameEvent_);
    if (previewThread_.joinable()) {
        previewThread_.join();
    }
    if (hPreviewFrameEvent_) {
        CloseHandle(hPreviewFrameEvent_);
        hPreviewFrameEvent_ = nullptr;
    }
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
    // QR Code Area: (25, 20, 270, 240)
    // Join URL Edit control: (25, 268, 270, 24)
    hUrlEdit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
        25, 268, 270, 24,
        hWnd_, nullptr, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hUrlEdit_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    // Status Label: (25, 296, 270, 36)
    hStatusLabel_ = CreateWindowExW(
        0, L"STATIC", L"初期化中...",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        25, 296, 270, 36,
        hWnd_, nullptr, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hStatusLabel_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    // Audio Output Label & Dropdown
    hAudioLabel_ = CreateWindowExW(0, L"STATIC", L"音声出力先 (VB-CABLE CABLE Input):", WS_CHILD | WS_VISIBLE, 25, 336, 270, 16, hWnd_, nullptr, hInstance, nullptr);
    if (hFont_) SendMessageW(hAudioLabel_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hAudioCombo_ = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        25, 354, 270, 150,
        hWnd_, (HMENU)(INT_PTR)ID_AUDIO_COMBO, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hAudioCombo_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    // Rotation Label, Dropdown & Quick Buttons
    hRotationLabel_ = CreateWindowExW(0, L"STATIC", L"映像回転 (Receiver Rotation):", WS_CHILD | WS_VISIBLE, 25, 384, 270, 16, hWnd_, nullptr, hInstance, nullptr);
    if (hFont_) SendMessageW(hRotationLabel_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hRotationCombo_ = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        25, 402, 140, 150,
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
        170, 402, 60, 24,
        hWnd_, (HMENU)(INT_PTR)ID_ROT_LEFT_BTN, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hRotLeftBtn_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hRotRightBtn_ = CreateWindowExW(
        0, L"BUTTON", L"↻ 右90°",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        235, 402, 60, 24,
        hWnd_, (HMENU)(INT_PTR)ID_ROT_RIGHT_BTN, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hRotRightBtn_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    // Virtual Camera Status Label
    hVcamStatusLabel_ = CreateWindowExW(
        0, L"STATIC", L"仮想カメラ: 確認中...",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        25, 432, 270, 18,
        hWnd_, nullptr, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hVcamStatusLabel_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    // Virtual Camera toggle button & New Session button
    hVcamButton_ = CreateWindowExW(
        0, L"BUTTON", L"仮想カメラ開始",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        25, 454, 130, 30,
        hWnd_, (HMENU)(INT_PTR)ID_VCAM_BTN, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hVcamButton_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hNewSessionBtn_ = CreateWindowExW(
        0, L"BUTTON", L"新しいセッション",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        165, 454, 130, 30,
        hWnd_, (HMENU)(INT_PTR)ID_NEW_SESSION_BTN, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hNewSessionBtn_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    // Register VCam (UAC) & Check Cameras buttons
    hRegisterVcamBtn_ = CreateWindowExW(
        0, L"BUTTON", L"システム登録 (UAC)",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        25, 490, 130, 30,
        hWnd_, (HMENU)(INT_PTR)ID_REGISTER_VCAM_BTN, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hRegisterVcamBtn_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hCheckCamerasBtn_ = CreateWindowExW(
        0, L"BUTTON", L"カメラ一覧確認",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        165, 490, 130, 30,
        hWnd_, (HMENU)(INT_PTR)ID_CHECK_CAMERAS_BTN, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hCheckCamerasBtn_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    // Test Pattern toggle button
    hTestPatternBtn_ = CreateWindowExW(
        0, L"BUTTON", L"🎬 テスト映像注入 (カラーバー)",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        25, 526, 270, 30,
        hWnd_, (HMENU)(INT_PTR)ID_TEST_PATTERN_BTN, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hTestPatternBtn_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    // Remote Camera Controls (Torch, Switch Camera, Zoom)
    hTorchBtn_ = CreateWindowExW(
        0, L"BUTTON", L"🔦 ライト [消灯]",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        25, 562, 130, 30,
        hWnd_, (HMENU)(INT_PTR)ID_TORCH_BTN, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hTorchBtn_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hSwitchCamBtn_ = CreateWindowExW(
        0, L"BUTTON", L"🔄 カメラ反転",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        165, 562, 130, 30,
        hWnd_, (HMENU)(INT_PTR)ID_SWITCH_CAM_BTN, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hSwitchCamBtn_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hZoom1xBtn_ = CreateWindowExW(
        0, L"BUTTON", L"🔍 1.0x",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        25, 598, 85, 28,
        hWnd_, (HMENU)(INT_PTR)ID_ZOOM_1X_BTN, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hZoom1xBtn_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hZoom2xBtn_ = CreateWindowExW(
        0, L"BUTTON", L"🔍 2.0x",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        117, 598, 85, 28,
        hWnd_, (HMENU)(INT_PTR)ID_ZOOM_2X_BTN, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hZoom2xBtn_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hZoom3xBtn_ = CreateWindowExW(
        0, L"BUTTON", L"🔍 3.0x",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        210, 598, 85, 28,
        hWnd_, (HMENU)(INT_PTR)ID_ZOOM_3X_BTN, hInstance, nullptr
    );
    if (hFont_) SendMessageW(hZoom3xBtn_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    // Create Right Panel: D3D11 Video Preview window (315, 20, 755, 610)
    hPreviewWnd_ = CreateWindowExW(
        0, wcChild.lpszClassName, L"",
        WS_CHILD | WS_VISIBLE,
        315, 20, 755, 610,
        hWnd_, nullptr, hInstance, nullptr
    );

    d3dPreview_.Initialize(hPreviewWnd_, 755, 610);
    isPreviewWorkerRunning_ = true;
    previewThread_ = std::thread(&MainWindow::PreviewWorkerProc, this);
    return true;
}

void MainWindow::PreviewWorkerProc() {
    std::vector<uint8_t> localBuffer;
    while (isPreviewWorkerRunning_) {
        WaitForSingleObject(hPreviewFrameEvent_, 100);
        if (!isPreviewWorkerRunning_) break;

        AcquireSRWLockShared(&previewSrwLock_);
        if (!previewBuffer_.empty()) {
            if (localBuffer.size() != previewBuffer_.size()) {
                localBuffer.resize(previewBuffer_.size());
            }
            std::memcpy(localBuffer.data(), previewBuffer_.data(), previewBuffer_.size());
        }
        ReleaseSRWLockShared(&previewSrwLock_);

        if (!localBuffer.empty()) {
            d3dPreview_.RenderNv12Frame(localBuffer, 1280, 720);
        }
    }
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

void MainWindow::SetVirtualCameraRegistered(bool isRegistered) {
    if (hVcamStatusLabel_) {
        if (isRegistered) {
            SetWindowTextW(hVcamStatusLabel_, L"● 仮想カメラ: 登録済み (正常認識)");
        } else {
            SetWindowTextW(hVcamStatusLabel_, L"○ 仮想カメラ: 未登録 (要システム登録)");
        }
    }
}

void MainWindow::SetTestPatternStatus(bool isTestPatternActive) {
    if (hTestPatternBtn_) {
        SetWindowTextW(hTestPatternBtn_, isTestPatternActive ? L"⏹ 実映像モードに戻す" : L"🎬 テスト映像注入 (カラーバー)");
    }
}

void MainWindow::SetTorchState(bool isEnabled) {
    isTorchOn_ = isEnabled;
    if (hTorchBtn_) {
        SetWindowTextW(hTorchBtn_, isEnabled ? L"🔦 ライト [点灯中]" : L"🔦 ライト [消灯]");
    }
}

void MainWindow::UpdateCameraCapabilities(bool supportsTorch, float minZoom, float maxZoom, float currentZoom, const std::string& facingMode) {
    if (hTorchBtn_) {
        EnableWindow(hTorchBtn_, supportsTorch ? TRUE : FALSE);
    }
    if (hSwitchCamBtn_) {
        SetWindowTextW(hSwitchCamBtn_, facingMode == "user" ? L"🔄 カメラ: 前面" : L"🔄 カメラ: 背面");
    }
}

void MainWindow::RenderPreviewFrame(std::span<const uint8_t> nv12Data) {
    if (nv12Data.empty()) return;
    AcquireSRWLockExclusive(&previewSrwLock_);
    if (previewBuffer_.size() != nv12Data.size()) {
        previewBuffer_.resize(nv12Data.size());
    }
    std::memcpy(previewBuffer_.data(), nv12Data.data(), nv12Data.size());
    ReleaseSRWLockExclusive(&previewSrwLock_);
    SetEvent(hPreviewFrameEvent_);
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

            // Draw QR code in left area: (25, 20, 270, 240)
            qrView_.Draw(hdc, 25, 20, 270, 240);

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
            } else if (wmId == ID_REGISTER_VCAM_BTN && wmEvent == BN_CLICKED) {
                if (onRegisterVirtualCamera_) {
                    onRegisterVirtualCamera_();
                }
            } else if (wmId == ID_CHECK_CAMERAS_BTN && wmEvent == BN_CLICKED) {
                if (onCheckCameras_) {
                    onCheckCameras_();
                }
            } else if (wmId == ID_TEST_PATTERN_BTN && wmEvent == BN_CLICKED) {
                if (onToggleTestPattern_) {
                    onToggleTestPattern_();
                }
            } else if (wmId == ID_TORCH_BTN && wmEvent == BN_CLICKED) {
                isTorchOn_ = !isTorchOn_;
                SetTorchState(isTorchOn_);
                if (onTorchToggle_) {
                    onTorchToggle_(isTorchOn_);
                }
            } else if (wmId == ID_SWITCH_CAM_BTN && wmEvent == BN_CLICKED) {
                if (onSwitchCamera_) {
                    onSwitchCamera_();
                }
            } else if (wmId == ID_ZOOM_1X_BTN && wmEvent == BN_CLICKED) {
                if (onZoomChange_) {
                    onZoomChange_(1.0f);
                }
            } else if (wmId == ID_ZOOM_2X_BTN && wmEvent == BN_CLICKED) {
                if (onZoomChange_) {
                    onZoomChange_(2.0f);
                }
            } else if (wmId == ID_ZOOM_3X_BTN && wmEvent == BN_CLICKED) {
                if (onZoomChange_) {
                    onZoomChange_(3.0f);
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
