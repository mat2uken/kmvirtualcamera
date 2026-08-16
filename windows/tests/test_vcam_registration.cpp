#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfvirtualcamera.h>
#include <iostream>
#include "../virtual-camera/media-source/webrtc_bridge_guids.h"

int main() {
    std::cout << "[TEST] Starting Virtual Camera API Diagnostic..." << std::endl;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::cout << "  CoInitializeEx(COINIT_MULTITHREADED): hr=0x" << std::hex << hr << std::dec << std::endl;

    hr = MFStartup(MF_VERSION);
    std::cout << "  MFStartup: hr=0x" << std::hex << hr << std::dec << std::endl;

    HMODULE hMfSensor = LoadLibraryW(L"mfsensorgroup.dll");
    std::cout << "  LoadLibrary(mfsensorgroup.dll): " << (hMfSensor ? "SUCCESS" : "FAIL") << std::endl;

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

            for (auto lt : lifetimes) {
                for (auto acc : accesses) {
                    std::cout << "\n  --- Testing Lifetime=" << lt << ", Access=" << acc << " ---" << std::endl;
                    IMFVirtualCamera* vcam = nullptr;
                    hr = pMFCreateVirtualCamera(
                        MFVirtualCameraType_SoftwareCameraSource,
                        lt,
                        acc,
                        L"KM Virtual Camera Test",
                        kClsidString,
                        nullptr,
                        0,
                        &vcam
                    );

                    std::cout << "  MFCreateVirtualCamera hr=0x" << std::hex << hr << std::dec << std::endl;

                    if (SUCCEEDED(hr) && vcam) {
                        std::cout << "  Calling vcam->Start(nullptr)..." << std::endl;
                    HRESULT hrStartNull = vcam->Start(nullptr);
                    std::cout << "  vcam->Start(nullptr) hr=0x" << std::hex << hrStartNull << std::dec << std::endl;

                    if (SUCCEEDED(hrStartNull)) {
                        std::cout << "  [SUCCESS] Virtual camera started synchronously!" << std::endl;
                        vcam->Stop();
                        vcam->Shutdown();
                        vcam->Release();
                        break;
                    }

                    DummyCallback cb;
                    HRESULT hrStart = vcam->Start(&cb);
                    std::cout << "  vcam->Start(&cb) hr=0x" << std::hex << hrStart << std::dec << std::endl;

                    if (SUCCEEDED(hrStart)) {
                        std::cout << "  [SUCCESS] Virtual camera started asynchronously!" << std::endl;
                        vcam->Stop();
                        vcam->Shutdown();
                        vcam->Release();
                        break;
                    }

                        if (SUCCEEDED(hrStart)) {
                            std::cout << "  [SUCCESS] Virtual camera started successfully!" << std::endl;
                            vcam->Stop();
                            vcam->Shutdown();
                            vcam->Release();
                            break;
                        }
                        vcam->Shutdown();
                        vcam->Release();
                    }
                }
            }
    }
    }

    MFShutdown();
    CoUninitialize();
    return 0;
}
