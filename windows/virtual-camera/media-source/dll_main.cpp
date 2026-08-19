#include <windows.h>
#include <timeapi.h>
#include <unknwn.h>
#include <atomic>
#include <new>
#include <stdio.h>
#include "webrtc_bridge_guids.h"
#include "webrtc_bridge_media_source.h"
#include "webrtc_bridge_activate.h"
#include "vcam_logger.h"
#include "module_lifetime.h"

static std::atomic<ULONG> g_serverLocks{0};
static HINSTANCE g_hInstance = nullptr;

class WebRtcBridgeClassFactory : public IClassFactory {
public:
    WebRtcBridgeClassFactory() { km::vcam::ModuleObjectCreated(); }
    virtual ~WebRtcBridgeClassFactory() { km::vcam::ModuleObjectDestroyed(); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override {
        return ++refCount_;
    }

    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG count = --refCount_;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    // IClassFactory
    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;

        wchar_t szGuid[64] = {0};
        StringFromGUID2(riid, szGuid, 64);
        LogVcam(L"[DLL CreateInstance] riid=%s", szGuid);

        if (riid == IID_IMFMediaSource || riid == IID_IMFMediaSourceEx) {
            Microsoft::WRL::ComPtr<IMFMediaSource> source;
            HRESULT hr = km::vcam::WebRtcBridgeMediaSource::CreateInstance(&source);
            LogVcam(L"  WebRtcBridgeMediaSource::CreateInstance hr=0x%08X", hr);
            if (FAILED(hr)) return hr;
            return source->QueryInterface(riid, ppv);
        }

        // Create Activate object (implements IMFActivate, IMFAttributes, and IUnknown)
        Microsoft::WRL::ComPtr<IMFActivate> activate;
        HRESULT hr = km::vcam::WebRtcBridgeActivate::CreateInstance(&activate);
        LogVcam(L"  WebRtcBridgeActivate::CreateInstance hr=0x%08X", hr);
        if (FAILED(hr)) return hr;

        return activate->QueryInterface(riid, ppv);
    }

    IFACEMETHODIMP LockServer(BOOL fLock) override {
        if (fLock) {
            ++g_serverLocks;
        } else {
            --g_serverLocks;
        }
        return S_OK;
    }

private:
    std::atomic<ULONG> refCount_{1};
};

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
        timeBeginPeriod(1);
        LogVcam(L"[DllMain] Process Attach pid=%u", GetCurrentProcessId());
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        timeEndPeriod(1);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    wchar_t szClsid[64] = {0};
    StringFromGUID2(rclsid, szClsid, 64);
    wchar_t szIid[64] = {0};
    StringFromGUID2(riid, szIid, 64);
    LogVcam(L"[DllGetClassObject] rclsid=%s riid=%s", szClsid, szIid);

    if (rclsid != CLSID_WebRtcBridgeVirtualCameraMediaSource) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    WebRtcBridgeClassFactory* pFactory = new (std::nothrow) WebRtcBridgeClassFactory();
    if (!pFactory) return E_OUTOFMEMORY;

    HRESULT hr = pFactory->QueryInterface(riid, ppv);
    pFactory->Release();
    return hr;
}

STDAPI DllCanUnloadNow(void) {
    return (g_serverLocks == 0 && km::vcam::g_moduleObjectCount == 0) ? S_OK : S_FALSE;
}

static HRESULT SetRegistryKeyAndValue(HKEY hKeyParent, const wchar_t* subKey, const wchar_t* valueName, const wchar_t* data) {
    HKEY hKey = nullptr;
    LONG res = RegCreateKeyExW(hKeyParent, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (res != ERROR_SUCCESS) return HRESULT_FROM_WIN32(res);

    if (data) {
        res = RegSetValueExW(hKey, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(data), static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(hKey);
    return HRESULT_FROM_WIN32(res);
}

STDAPI DllRegisterServer(void) {
    wchar_t modulePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(g_hInstance, modulePath, MAX_PATH)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    std::wstring clsidPath = L"CLSID\\";
    clsidPath += kClsidString;
    std::wstring inprocPath = clsidPath + L"\\InProcServer32";

    HRESULT hr = SetRegistryKeyAndValue(HKEY_CLASSES_ROOT, clsidPath.c_str(), nullptr, kFriendlyName);
    if (FAILED(hr)) return hr;

    hr = SetRegistryKeyAndValue(HKEY_CLASSES_ROOT, inprocPath.c_str(), nullptr, modulePath);
    if (FAILED(hr)) return hr;

    hr = SetRegistryKeyAndValue(HKEY_CLASSES_ROOT, inprocPath.c_str(), L"ThreadingModel", L"Both");
    if (FAILED(hr)) return hr;

    return S_OK;
}

STDAPI DllUnregisterServer(void) {
    std::wstring clsidPath = L"CLSID\\";
    clsidPath += kClsidString;
    std::wstring inprocPath = clsidPath + L"\\InProcServer32";

    // Remove category keys created by older builds. MFCreateVirtualCamera owns
    // camera-device registration; the COM class itself must not masquerade as
    // a DirectShow filter or Media Foundation transform.
    std::wstring dshowCatPath = L"CLSID\\{860BB310-5D01-11d0-BD3B-00A0C911CE86}\\Instance\\";
    dshowCatPath += kClsidString;
    RegDeleteKeyW(HKEY_CLASSES_ROOT, dshowCatPath.c_str());

    std::wstring mfCat1 = L"MediaFoundation\\Transforms\\Categories\\{E5323777-F976-4F5B-9B55-B94699C46E44}\\";
    mfCat1 += kClsidString;
    RegDeleteKeyW(HKEY_CLASSES_ROOT, mfCat1.c_str());

    std::wstring mfCat2 = L"MediaFoundation\\Transforms\\Categories\\{65E8773D-8F56-11D0-A3B9-00A0C9223196}\\";
    mfCat2 += kClsidString;
    RegDeleteKeyW(HKEY_CLASSES_ROOT, mfCat2.c_str());

    RegDeleteKeyW(HKEY_CLASSES_ROOT, inprocPath.c_str());
    LONG res = RegDeleteKeyW(HKEY_CLASSES_ROOT, clsidPath.c_str());
    return (res == ERROR_SUCCESS || res == ERROR_FILE_NOT_FOUND) ? S_OK : HRESULT_FROM_WIN32(res);
}
