#pragma once

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfvirtualcamera.h>
#include <wrl/client.h>
#include <string>
#include <vector>

namespace km::vcam {

struct CameraDeviceInfo {
    std::wstring friendlyName;
    std::wstring symbolicLink;
    bool isVirtualCamera{false};
};

class VirtualCameraRegistrar {
public:
    VirtualCameraRegistrar();
    ~VirtualCameraRegistrar();

    bool StartVirtualCamera(const std::wstring& cameraFriendlyName = L"WebRTC Bridge Virtual Camera");
    void StopVirtualCamera();
    bool IsRunning() const { return isRunning_; }

    // Enumerate all system video capture devices recognized by Windows Media Foundation
    static std::vector<CameraDeviceInfo> EnumerateSystemCameras();

    // Check if the WebRTC Bridge Virtual Camera is registered in the system registry (HKLM / HKCU)
    static bool IsVirtualCameraRegistered();

    // Trigger elevated (UAC) registration from the app
    static bool RegisterVirtualCameraWithElevation(HWND hWndParent = nullptr);

private:
    bool isRunning_{false};
    Microsoft::WRL::ComPtr<IMFVirtualCamera> virtualCamera_;
};

} // namespace km::vcam
