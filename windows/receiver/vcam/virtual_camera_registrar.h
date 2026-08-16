#pragma once

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfvirtualcamera.h>
#include <wrl/client.h>
#include <string>

namespace km::vcam {

class VirtualCameraRegistrar {
public:
    VirtualCameraRegistrar();
    ~VirtualCameraRegistrar();

    bool StartVirtualCamera(const std::wstring& cameraFriendlyName = L"WebRTC Bridge Windows Virtual Camera");
    void StopVirtualCamera();
    bool IsRunning() const { return isRunning_; }

private:
    bool isRunning_{false};
    Microsoft::WRL::ComPtr<IMFVirtualCamera> virtualCamera_;
};

} // namespace km::vcam
