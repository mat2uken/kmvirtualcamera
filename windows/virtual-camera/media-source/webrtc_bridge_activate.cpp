#include "webrtc_bridge_activate.h"
#include "webrtc_bridge_guids.h"
#include "vcam_logger.h"
#include "module_lifetime.h"
#include <mfvirtualcamera.h>
#include <new>
#include <wrl/client.h>

namespace km::vcam {

HRESULT WebRtcBridgeActivate::CreateInstance(IMFActivate** ppActivate) {
    if (!ppActivate) return E_POINTER;
    *ppActivate = nullptr;

    WebRtcBridgeActivate* pActivate = new (std::nothrow) WebRtcBridgeActivate();
    if (!pActivate) return E_OUTOFMEMORY;

    HRESULT hr = WebRtcBridgeMediaSource::CreateInstance(&pActivate->activeSource_);
    if (FAILED(hr)) {
        pActivate->Release();
        return hr;
    }

    *ppActivate = pActivate;
    return S_OK;
}

WebRtcBridgeActivate::WebRtcBridgeActivate() {
    ModuleObjectCreated();
    MFCreateAttributes(&attributes_, 16);
    if (attributes_) {
        attributes_->SetGUID(MFT_TRANSFORM_CLSID_Attribute, CLSID_WebRtcBridgeVirtualCameraMediaSource);
        attributes_->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        attributes_->SetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, kFriendlyName);
    }
}

WebRtcBridgeActivate::~WebRtcBridgeActivate() {
    // Note: Do NOT call ShutdownObject() here.
    // The activated MediaSource instance is owned and managed by the consumer (FrameServer/SourceReader).
    ModuleObjectDestroyed();
}

IFACEMETHODIMP WebRtcBridgeActivate::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    wchar_t szGuid[64] = {0};
    StringFromGUID2(riid, szGuid, 64);
    LogVcam(L"  [WebRtcBridgeActivate::QI] riid=%s", szGuid);

    if (riid == IID_IUnknown) {
        *ppv = static_cast<IUnknown*>(static_cast<IMFActivate*>(this));
    } else if (riid == IID_IMFActivate) {
        *ppv = static_cast<IMFActivate*>(this);
    } else if (riid == IID_IMFAttributes) {
        *ppv = static_cast<IMFAttributes*>(this);
    } else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) WebRtcBridgeActivate::AddRef() {
    return ++refCount_;
}

IFACEMETHODIMP_(ULONG) WebRtcBridgeActivate::Release() {
    ULONG count = --refCount_;
    if (count == 0) {
        delete this;
    }
    return count;
}

// IMFAttributes delegation
IFACEMETHODIMP WebRtcBridgeActivate::GetItem(REFGUID guidKey, PROPVARIANT* pValue) {
    HRESULT hr = attributes_ ? attributes_->GetItem(guidKey, pValue) : E_UNEXPECTED;
    if (FAILED(hr)) {
        wchar_t szG[64] = {0}; StringFromGUID2(guidKey, szG, 64);
        wprintf(L"    [Activate GetItem FAIL] key=%s hr=0x%x\n", szG, hr);
    }
    return hr;
}

IFACEMETHODIMP WebRtcBridgeActivate::GetItemType(REFGUID guidKey, MF_ATTRIBUTE_TYPE* pType) {
    return attributes_ ? attributes_->GetItemType(guidKey, pType) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::CompareItem(REFGUID guidKey, REFPROPVARIANT Value, BOOL* pbResult) {
    return attributes_ ? attributes_->CompareItem(guidKey, Value, pbResult) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::Compare(IMFAttributes* pTheirs, MF_ATTRIBUTES_MATCH_TYPE MatchType, BOOL* pbResult) {
    return attributes_ ? attributes_->Compare(pTheirs, MatchType, pbResult) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::GetUINT32(REFGUID guidKey, UINT32* punValue) {
    HRESULT hr = attributes_ ? attributes_->GetUINT32(guidKey, punValue) : E_UNEXPECTED;
    if (FAILED(hr)) {
        wchar_t szG[64] = {0}; StringFromGUID2(guidKey, szG, 64);
        wprintf(L"    [Activate GetUINT32 FAIL] key=%s hr=0x%x\n", szG, hr);
    }
    return hr;
}

IFACEMETHODIMP WebRtcBridgeActivate::GetUINT64(REFGUID guidKey, UINT64* punValue) {
    HRESULT hr = attributes_ ? attributes_->GetUINT64(guidKey, punValue) : E_UNEXPECTED;
    if (FAILED(hr)) {
        wchar_t szG[64] = {0}; StringFromGUID2(guidKey, szG, 64);
        wprintf(L"    [Activate GetUINT64 FAIL] key=%s hr=0x%x\n", szG, hr);
    }
    return hr;
}

IFACEMETHODIMP WebRtcBridgeActivate::GetDouble(REFGUID guidKey, double* pfValue) {
    return attributes_ ? attributes_->GetDouble(guidKey, pfValue) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::GetGUID(REFGUID guidKey, GUID* pguidValue) {
    HRESULT hr = attributes_ ? attributes_->GetGUID(guidKey, pguidValue) : E_UNEXPECTED;
    if (FAILED(hr)) {
        wchar_t szG[64] = {0}; StringFromGUID2(guidKey, szG, 64);
        wprintf(L"    [Activate GetGUID FAIL] key=%s hr=0x%x\n", szG, hr);
    }
    return hr;
}

IFACEMETHODIMP WebRtcBridgeActivate::GetStringLength(REFGUID guidKey, UINT32* pcchLength) {
    HRESULT hr = attributes_ ? attributes_->GetStringLength(guidKey, pcchLength) : E_UNEXPECTED;
    if (FAILED(hr)) {
        wchar_t szG[64] = {0}; StringFromGUID2(guidKey, szG, 64);
        wprintf(L"    [Activate GetStringLength FAIL] key=%s hr=0x%x\n", szG, hr);
    }
    return hr;
}

IFACEMETHODIMP WebRtcBridgeActivate::GetString(REFGUID guidKey, LPWSTR pwszValue, UINT32 cchBufSize, UINT32* pcchLength) {
    HRESULT hr = attributes_ ? attributes_->GetString(guidKey, pwszValue, cchBufSize, pcchLength) : E_UNEXPECTED;
    if (FAILED(hr)) {
        wchar_t szG[64] = {0}; StringFromGUID2(guidKey, szG, 64);
        wprintf(L"    [Activate GetString FAIL] key=%s hr=0x%x\n", szG, hr);
    }
    return hr;
}

IFACEMETHODIMP WebRtcBridgeActivate::GetAllocatedString(REFGUID guidKey, LPWSTR* ppwszValue, UINT32* pcchLength) {
    return attributes_ ? attributes_->GetAllocatedString(guidKey, ppwszValue, pcchLength) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::GetBlobSize(REFGUID guidKey, UINT32* pcbBlobSize) {
    return attributes_ ? attributes_->GetBlobSize(guidKey, pcbBlobSize) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::GetBlob(REFGUID guidKey, UINT8* pBuf, UINT32 cbBufSize, UINT32* pcbBlobSize) {
    return attributes_ ? attributes_->GetBlob(guidKey, pBuf, cbBufSize, pcbBlobSize) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::GetAllocatedBlob(REFGUID guidKey, UINT8** ppBuf, UINT32* pcbSize) {
    return attributes_ ? attributes_->GetAllocatedBlob(guidKey, ppBuf, pcbSize) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::GetUnknown(REFGUID guidKey, REFIID riid, LPVOID* ppv) {
    return attributes_ ? attributes_->GetUnknown(guidKey, riid, ppv) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::SetItem(REFGUID guidKey, REFPROPVARIANT Value) {
    return attributes_ ? attributes_->SetItem(guidKey, Value) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::DeleteItem(REFGUID guidKey) {
    return attributes_ ? attributes_->DeleteItem(guidKey) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::DeleteAllItems() {
    return attributes_ ? attributes_->DeleteAllItems() : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::SetUINT32(REFGUID guidKey, UINT32 unValue) {
    return attributes_ ? attributes_->SetUINT32(guidKey, unValue) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::SetUINT64(REFGUID guidKey, UINT64 unValue) {
    return attributes_ ? attributes_->SetUINT64(guidKey, unValue) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::SetDouble(REFGUID guidKey, double fValue) {
    return attributes_ ? attributes_->SetDouble(guidKey, fValue) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::SetGUID(REFGUID guidKey, REFGUID guidValue) {
    return attributes_ ? attributes_->SetGUID(guidKey, guidValue) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::SetString(REFGUID guidKey, LPCWSTR wszValue) {
    return attributes_ ? attributes_->SetString(guidKey, wszValue) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::SetBlob(REFGUID guidKey, const UINT8* pBuf, UINT32 cbBufSize) {
    return attributes_ ? attributes_->SetBlob(guidKey, pBuf, cbBufSize) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::SetUnknown(REFGUID guidKey, IUnknown* pUnknown) {
    return attributes_ ? attributes_->SetUnknown(guidKey, pUnknown) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::LockStore() {
    return attributes_ ? attributes_->LockStore() : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::UnlockStore() {
    return attributes_ ? attributes_->UnlockStore() : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::GetCount(UINT32* pcItems) {
    return attributes_ ? attributes_->GetCount(pcItems) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::GetItemByIndex(UINT32 unIndex, GUID* pguidKey, PROPVARIANT* pValue) {
    return attributes_ ? attributes_->GetItemByIndex(unIndex, pguidKey, pValue) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeActivate::CopyAllItems(IMFAttributes* pDest) {
    return attributes_ ? attributes_->CopyAllItems(pDest) : E_UNEXPECTED;
}

// IMFActivate implementation
IFACEMETHODIMP WebRtcBridgeActivate::ActivateObject(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    wchar_t szGuid[64] = {0};
    StringFromGUID2(riid, szGuid, 64);
    LogVcam(L"[WebRtcBridgeActivate::ActivateObject] riid=%s", szGuid);

    std::lock_guard<std::mutex> lock(lock_);
    if (!activeSource_) {
        HRESULT hr = WebRtcBridgeMediaSource::CreateInstance(&activeSource_);
        LogVcam(L"  WebRtcBridgeMediaSource::CreateInstance hr=0x%08X", hr);
        if (FAILED(hr)) return hr;
    }

    HRESULT hr = activeSource_->QueryInterface(riid, ppv);
    LogVcam(L"  activeSource_->QueryInterface hr=0x%08X", hr);
    return hr;
}

IFACEMETHODIMP WebRtcBridgeActivate::ShutdownObject() {
    LogVcam(L"[WebRtcBridgeActivate::ShutdownObject]");
    std::lock_guard<std::mutex> lock(lock_);
    if (activeSource_) {
        activeSource_->Shutdown();
        activeSource_.Reset();
    }
    return S_OK;
}

IFACEMETHODIMP WebRtcBridgeActivate::DetachObject() {
    LogVcam(L"[WebRtcBridgeActivate::DetachObject]");
    std::lock_guard<std::mutex> lock(lock_);
    activeSource_.Reset();
    return S_OK;
}

} // namespace km::vcam
