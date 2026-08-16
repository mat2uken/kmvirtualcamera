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

    HRESULT hrMf = MFStartup(MF_VERSION);

    HMODULE hMfSensor = GetModuleHandleW(L"mfsensorgroup.dll");
    if (!hMfSensor) {
        hMfSensor = LoadLibraryW(L"mfsensorgroup.dll");
    }
    if (!hMfSensor) {
        hMfSensor = LoadLibraryW(L"mfplat.dll");
    }
    if (!hMfSensor) return false;

    auto pMFCreateVirtualCamera = reinterpret_cast<MFCreateVirtualCameraFn>(
        GetProcAddress(hMfSensor, "MFCreateVirtualCamera")
    );
    if (!pMFCreateVirtualCamera) return false;

    GUID categories[] = {
        { 0xE5323777, 0xF976, 0x4f5b, { 0x9B, 0x55, 0xB9, 0x46, 0x99, 0xC4, 0x6E, 0x44 } }, // KSCATEGORY_VIDEO_CAMERA
        { 0x65E8773D, 0x8F56, 0x11D0, { 0xA3, 0xB9, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 } }  // KSCATEGORY_CAPTURE
    };

    HRESULT hr = pMFCreateVirtualCamera(
        MFVirtualCameraType_SoftwareCameraSource,
        MFVirtualCameraLifetime_Session,
        MFVirtualCameraAccess_CurrentUser,
        cameraFriendlyName.c_str(),
        kClsidString,
        categories,
        2,
        &virtualCamera_
    );

    if (FAILED(hr) || !virtualCamera_) {
        // Fallback without categories
        hr = pMFCreateVirtualCamera(
            MFVirtualCameraType_SoftwareCameraSource,
            MFVirtualCameraLifetime_Session,
            MFVirtualCameraAccess_CurrentUser,
            cameraFriendlyName.c_str(),
            kClsidString,
            nullptr,
            0,
            &virtualCamera_
        );
    }

    if (FAILED(hr) || !virtualCamera_) {
        return false;
    }

    hr = virtualCamera_->Start(nullptr);
    if (FAILED(hr)) {
        // Session registration is active even if FrameServer synchronous start is deferred
        // Keep virtualCamera_ alive so the COM registration remains registered for the session
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
    MFShutdown();
}

} // namespace km::vcam
