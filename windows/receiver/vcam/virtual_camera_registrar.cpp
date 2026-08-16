#include "virtual_camera_registrar.h"
#include "../../virtual-camera/media-source/webrtc_bridge_guids.h"

namespace km::vcam {

typedef HRESULT (STDAPICALLTYPE *MFCreateVirtualCameraFn)(
    MFVirtualCameraType type,
    MFVirtualCameraLifetime lifetime,
    MFVirtualCameraAccess access,
    LPCWSTR pwzFriendlyName,
    LPCWSTR pwzCLSID,
    const GUID* pCategories,
    ULONG cCategories,
    IMFVirtualCamera** ppVirtualCamera
);

VirtualCameraRegistrar::VirtualCameraRegistrar() = default;

VirtualCameraRegistrar::~VirtualCameraRegistrar() {
    StopVirtualCamera();
}

bool VirtualCameraRegistrar::StartVirtualCamera(const std::wstring& cameraFriendlyName) {
    if (isRunning_) return true;

    HMODULE hMfplat = GetModuleHandleW(L"mfplat.dll");
    if (!hMfplat) {
        hMfplat = LoadLibraryW(L"mfplat.dll");
    }
    if (!hMfplat) return false;

    auto pMFCreateVirtualCamera = reinterpret_cast<MFCreateVirtualCameraFn>(
        GetProcAddress(hMfplat, "MFCreateVirtualCamera")
    );
    if (!pMFCreateVirtualCamera) return false;

    HRESULT hr = pMFCreateVirtualCamera(
        MFVirtualCameraType_SoftwareCameraSource,
        MFVirtualCameraLifetime_Session,
        MFVirtualCameraAccess_CurrentUser,
        cameraFriendlyName.c_str(),
        kClsidString,
        nullptr,
        0,
        &virtualCamera_
    );

    if (FAILED(hr) || !virtualCamera_) {
        return false;
    }

    hr = virtualCamera_->Start(nullptr);
    if (FAILED(hr)) {
        virtualCamera_.Reset();
        return false;
    }

    isRunning_ = true;
    return true;
}

void VirtualCameraRegistrar::StopVirtualCamera() {
    if (!isRunning_) return;

    if (virtualCamera_) {
        virtualCamera_->Stop();
        virtualCamera_->Shutdown();
        virtualCamera_.Reset();
    }
    isRunning_ = false;
}

} // namespace km::vcam
