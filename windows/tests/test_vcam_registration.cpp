#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfvirtualcamera.h>
#include <iostream>
#include "../virtual-camera/media-source/webrtc_bridge_guids.h"
#include "../receiver/vcam/virtual_camera_registrar.h"

int main(int argc, char* argv[]) {
    bool isRegisterCmd = false;
    bool isUnregisterCmd = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--register") == 0 || strcmp(argv[i], "-r") == 0) isRegisterCmd = true;
        if (strcmp(argv[i], "--unregister") == 0 || strcmp(argv[i], "-u") == 0) isUnregisterCmd = true;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    hr = MFStartup(MF_VERSION);

    HMODULE hMfSensor = LoadLibraryW(L"mfsensorgroup.dll");
    if (!hMfSensor) hMfSensor = LoadLibraryW(L"mfplat.dll");

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

    auto pMFCreateVirtualCamera = hMfSensor ? reinterpret_cast<MFCreateVirtualCameraFn>(
        GetProcAddress(hMfSensor, "MFCreateVirtualCamera")
    ) : nullptr;

    if (isRegisterCmd || isUnregisterCmd) {
        wprintf(isUnregisterCmd
            ? L"[UNREGISTER] Removing System Virtual Camera from Windows PnP...\n"
            : L"[REGISTER] Registering System Virtual Camera into Windows PnP...\n");
        if (pMFCreateVirtualCamera) {
            IMFVirtualCamera* vcam = nullptr;
            hr = pMFCreateVirtualCamera(
                MFVirtualCameraType_SoftwareCameraSource,
                MFVirtualCameraLifetime_System,
                MFVirtualCameraAccess_AllUsers,
                L"WebRTC Bridge Virtual Camera",
                kClsidString,
                nullptr,
                0,
                &vcam
            );
            if (FAILED(hr)) {
                // Try with CurrentUser access
                hr = pMFCreateVirtualCamera(
                    MFVirtualCameraType_SoftwareCameraSource,
                    MFVirtualCameraLifetime_System,
                    MFVirtualCameraAccess_CurrentUser,
                    L"WebRTC Bridge Virtual Camera",
                    kClsidString,
                    nullptr,
                    0,
                    &vcam
                );
            }
            if (SUCCEEDED(hr) && vcam) {
                if (isUnregisterCmd) {
                    hr = vcam->Remove();
                    wprintf(L"[UNREGISTER] Virtual Camera remove hr=0x%x\n", hr);
                    vcam->Shutdown();
                    vcam->Release();
                    return SUCCEEDED(hr) ? 0 : 1;
                }
                hr = vcam->Start(nullptr);
                wprintf(L"[REGISTER] Virtual Camera device created and started! hr=0x%x\n", hr);
                vcam->Shutdown();
                vcam->Release();
                return SUCCEEDED(hr) ? 0 : 1;
            } else {
                wprintf(L"[REGISTER] Failed to create virtual camera: hr=0x%x\n", hr);
                return 1;
            }
        }
        return 1;
    }

    std::cout << "[TEST] Starting Virtual Camera API Diagnostic..." << std::endl;

    if (hMfSensor) {
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

        auto pMFCreateVirtualCamera = reinterpret_cast<MFCreateVirtualCameraFn>(
            GetProcAddress(hMfSensor, "MFCreateVirtualCamera")
        );

        typedef HRESULT (STDAPICALLTYPE *MFIsVirtualCameraTypeSupportedFn)(
            MFVirtualCameraType type,
            BOOL* supported
        );
        auto pMFIsVirtualCameraTypeSupported = reinterpret_cast<MFIsVirtualCameraTypeSupportedFn>(
            GetProcAddress(hMfSensor, "MFIsVirtualCameraTypeSupported")
        );
        if (pMFIsVirtualCameraTypeSupported) {
            BOOL sup0 = FALSE;
            pMFIsVirtualCameraTypeSupported(MFVirtualCameraType_SoftwareCameraSource, &sup0);
            std::cout << "  MFIsVirtualCameraTypeSupported(SoftwareCameraSource): " << (sup0 ? "YES" : "NO") << std::endl;
        }

        if (pMFCreateVirtualCamera) {
            // 1. Test CoCreateInstance directly on the CLSID
            IUnknown* pTestUnk = nullptr;
            HRESULT hrCo = CoCreateInstance(CLSID_WebRtcBridgeVirtualCameraMediaSource, nullptr, CLSCTX_INPROC_SERVER, IID_IUnknown, (void**)&pTestUnk);
            std::cout << "  CoCreateInstance(IUnknown): hr=0x" << std::hex << hrCo << std::dec << std::endl;
            if (pTestUnk) {
                IMFActivate* pAct = nullptr;
                HRESULT hrAct = pTestUnk->QueryInterface(IID_IMFActivate, (void**)&pAct);
                std::cout << "  pTestUnk->QueryInterface(IMFActivate): hr=0x" << std::hex << hrAct << std::dec << std::endl;
                if (pAct) pAct->Release();

                IMFMediaSource* pSrc = nullptr;
                HRESULT hrSrc = pTestUnk->QueryInterface(IID_IMFMediaSource, (void**)&pSrc);
                std::cout << "  pTestUnk->QueryInterface(IMFMediaSource): hr=0x" << std::hex << hrSrc << std::dec << std::endl;
                if (pSrc) pSrc->Release();

                pTestUnk->Release();
            }

            class DummyCallback : public IMFAsyncCallback {
            public:
                IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
                    if (!ppv) return E_POINTER;
                    wchar_t gstr[64] = {0};
                    StringFromGUID2(riid, gstr, 64);
                    std::wcout << L"    [DummyCallback QI] " << gstr << std::endl;
                    if (riid == IID_IUnknown || riid == IID_IMFAsyncCallback) {
                        *ppv = static_cast<IMFAsyncCallback*>(this);
                        AddRef();
                        return S_OK;
                    }
                    *ppv = nullptr;
                    return E_NOINTERFACE;
                }
                IFACEMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
                IFACEMETHODIMP_(ULONG) Release() override { return --ref_; }
                IFACEMETHODIMP GetParameters(DWORD* pdwFlags, DWORD* pdwQueue) override {
                    if (pdwFlags) *pdwFlags = 0;
                    if (pdwQueue) *pdwQueue = MFASYNC_CALLBACK_QUEUE_STANDARD;
                    return S_OK;
                }
                IFACEMETHODIMP Invoke(IMFAsyncResult* pAsyncResult) override {
                    std::cout << "  [Callback Invoke] Virtual Camera callback invoked!" << std::endl;
                    SetEvent(hEvent_);
                    return S_OK;
                }
                HANDLE hEvent_{ CreateEvent(nullptr, TRUE, FALSE, nullptr) };
                std::atomic<ULONG> ref_{1};
            };

            MFVirtualCameraLifetime lifetimes[] = { MFVirtualCameraLifetime_Session, MFVirtualCameraLifetime_System };
            MFVirtualCameraAccess accesses[] = { MFVirtualCameraAccess_CurrentUser, MFVirtualCameraAccess_AllUsers };

            GUID catVideoCamera = { 0xE5323777, 0xF976, 0x4f5b, { 0x9B, 0x55, 0xB9, 0x46, 0x99, 0xC4, 0x6E, 0x44 } }; // KSCATEGORY_VIDEO_CAMERA
            GUID catCapture     = { 0x65E8773D, 0x8F56, 0x11D0, { 0xA3, 0xB9, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 } }; // KSCATEGORY_CAPTURE
            GUID catSensorCam   = { 0x24E552D7, 0x6523, 0x47F7, { 0xA6, 0x47, 0xD3, 0x46, 0x5B, 0xF1, 0xF5, 0xCA } }; // KSCATEGORY_SENSOR_CAMERA
            GUID allCats[] = { catVideoCamera, catCapture, catSensorCam };

            const wchar_t* clsidFormats[] = {
                L"{84BA9D22-C5E5-4674-8848-A979BD2764B2}",
                L"84BA9D22-C5E5-4674-8848-A979BD2764B2",
                L"{84ba9d22-c5e5-4674-8848-a979bd2764b2}"
            };

            for (auto clsidStr : clsidFormats) {
                std::wcout << L"\n========================================\nTesting CLSID string: " << clsidStr << std::endl;
                for (int catMode = 0; catMode < 3; ++catMode) {
                    const GUID* pCat = (catMode == 0) ? nullptr : (catMode == 1 ? allCats : &catVideoCamera);
                    ULONG numCats = (catMode == 0) ? 0 : (catMode == 1 ? 3 : 1);
                    std::cout << "  Category Mode: " << catMode << " (numCats=" << numCats << ")" << std::endl;

                    for (auto lt : lifetimes) {
                        for (auto acc : accesses) {
                            IMFVirtualCamera* vcam = nullptr;
                            hr = pMFCreateVirtualCamera(
                                MFVirtualCameraType_SoftwareCameraSource,
                                lt,
                                acc,
                                L"WebRTC Bridge Virtual Camera",
                                clsidStr,
                                pCat,
                                numCats,
                                &vcam
                            );

                            if (SUCCEEDED(hr) && vcam) {
                                HRESULT hrStartNull = vcam->Start(nullptr);
                                std::cout << "    [LT=" << lt << ", ACC=" << acc << "] vcam->Start(null): hr=0x" << std::hex << hrStartNull << std::dec << std::endl;

                                if (SUCCEEDED(hrStartNull)) {
                                    std::cout << "    >>> SUCCESSFUL START! <<<" << std::endl;
                                    // Re-enumerate to verify device presence while virtual camera is running
                                    auto runningCams = km::vcam::VirtualCameraRegistrar::EnumerateSystemCameras();
                                    std::cout << "    [Active Start] Found " << runningCams.size() << " system camera(s):" << std::endl;
                                    for (const auto& c : runningCams) {
                                        std::wcout << L"      * " << c.friendlyName << L" (SymLink: " << c.symbolicLink << L")" << std::endl;
                                    }
                                    vcam->Stop();
                                }
                                vcam->Shutdown();
                                vcam->Release();
                            } else {
                                std::cout << "    [LT=" << lt << ", ACC=" << acc << "] CreateVirtualCamera failed: hr=0x" << std::hex << hr << std::dec << std::endl;
                            }
                        }
                    }
                }
            }

            // Enumerate all system video capture devices using VirtualCameraRegistrar
            wprintf(L"\n  --- Testing VirtualCameraRegistrar::EnumerateSystemCameras() ---\n");
            auto cameras = km::vcam::VirtualCameraRegistrar::EnumerateSystemCameras();
            wprintf(L"  EnumerateSystemCameras returned %zu device(s):\n", cameras.size());
            for (size_t i = 0; i < cameras.size(); ++i) {
                wprintf(L"    [%zu] %ls\n         SymLink: %ls\n         Virtual: %ls\n",
                    (i + 1),
                    cameras[i].friendlyName.c_str(),
                    cameras[i].symbolicLink.c_str(),
                    cameras[i].isVirtualCamera ? L"MATCH" : L"NO");
            }

            bool isReg = km::vcam::VirtualCameraRegistrar::IsVirtualCameraRegistered();
            wprintf(L"  VirtualCameraRegistrar::IsVirtualCameraRegistered(): %ls\n", isReg ? L"TRUE" : L"FALSE");
        }
    }

    MFShutdown();
    CoUninitialize();
    return 0;
}
