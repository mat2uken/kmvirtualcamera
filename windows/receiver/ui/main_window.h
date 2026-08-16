#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include "qr_view.h"
#include "d3d11_preview.h"
#include "../audio/audio_device_enumerator.h"

namespace km::ui {

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    bool Create(HINSTANCE hInstance, int width = 1100, int height = 620);
    void Show(int nCmdShow);
    HWND GetHwnd() const { return hWnd_; }

    void SetJoinUrl(const std::string& joinUrl);
    void SetStatusText(const std::wstring& status);
    void SetAudioDevices(const std::vector<audio::AudioDevice>& devices, int selectedIndex);
    void SetVirtualCameraStatus(bool isStarted);
    void RenderPreviewFrame(std::span<const uint8_t> nv12Data);

    void SetOnAudioDeviceChanged(std::function<void(int index)> cb) { onAudioDeviceChanged_ = std::move(cb); }
    void SetOnToggleVirtualCamera(std::function<void()> cb) { onToggleVirtualCamera_ = std::move(cb); }
    void SetOnNewSession(std::function<void()> cb) { onNewSession_ = std::move(cb); }

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hWnd_{nullptr};
    HWND hPreviewWnd_{nullptr};
    HWND hAudioCombo_{nullptr};
    HWND hAudioLabel_{nullptr};
    HWND hVcamButton_{nullptr};
    HWND hNewSessionBtn_{nullptr};
    HWND hStatusLabel_{nullptr};
    HWND hUrlEdit_{nullptr};
    HFONT hFont_{nullptr};

    QrView qrView_;
    D3D11Preview d3dPreview_;

    std::function<void(int index)> onAudioDeviceChanged_;
    std::function<void()> onToggleVirtualCamera_;
    std::function<void()> onNewSession_;
};

} // namespace km::ui
