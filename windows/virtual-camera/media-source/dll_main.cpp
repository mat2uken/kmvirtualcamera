#include <windows.h>
#include <unknwn.h>
#include <atomic>
#include <new>
#include "webrtc_bridge_guids.h"
#include "webrtc_bridge_media_source.h"

static std::atomic<ULONG> g_serverLocks{0};
static HINSTANCE g_hInstance = nullptr;

class WebRtcBridgeClassFactory : public IClassFactory {
public:
    WebRtcBridgeClassFactory() = default;
    virtual ~WebRtcBridgeClassFactory() = default;

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

        Microsoft::WRL::ComPtr<IMFMediaSource> source;
        HRESULT hr = km::vcam::WebRtcBridgeMediaSource::CreateInstance(&source);
        if (FAILED(hr)) return hr;

        return source->QueryInterface(riid, ppv);
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
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

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
    return (g_serverLocks == 0) ? S_OK : S_FALSE;
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
    return hr;
}

STDAPI DllUnregisterServer(void) {
    std::wstring clsidPath = L"CLSID\\";
    clsidPath += kClsidString;
    std::wstring inprocPath = clsidPath + L"\\InProcServer32";

    RegDeleteKeyW(HKEY_CLASSES_ROOT, inprocPath.c_str());
    LONG res = RegDeleteKeyW(HKEY_CLASSES_ROOT, clsidPath.c_str());
    return (res == ERROR_SUCCESS || res == ERROR_FILE_NOT_FOUND) ? S_OK : HRESULT_FROM_WIN32(res);
}
