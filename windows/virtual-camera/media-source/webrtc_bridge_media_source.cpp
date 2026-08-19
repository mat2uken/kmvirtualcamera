#include "webrtc_bridge_media_source.h"
#include "webrtc_bridge_activate.h"
#include "webrtc_bridge_guids.h"
#include "vcam_logger.h"
#include "module_lifetime.h"
#include <mfvirtualcamera.h>

namespace km::vcam {

HRESULT WebRtcBridgeMediaSource::CreateInstance(IMFMediaSource** ppSource) {
    if (!ppSource) return E_POINTER;
    *ppSource = nullptr;

    WebRtcBridgeMediaSource* pSource = new (std::nothrow) WebRtcBridgeMediaSource();
    if (!pSource) return E_OUTOFMEMORY;

    HRESULT hr = pSource->Initialize();
    LogVcam(L"[WebRtcBridgeMediaSource::CreateInstance] Initialize hr=0x%08X", hr);
    if (FAILED(hr)) {
        pSource->Release();
        return hr;
    }

    *ppSource = pSource;
    return S_OK;
}

WebRtcBridgeMediaSource::WebRtcBridgeMediaSource() {
    ModuleObjectCreated();
    MFCreateEventQueue(&eventQueue_);
    MFCreateAttributes(&sourceAttributes_, 8);
    if (sourceAttributes_) {
        sourceAttributes_->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        sourceAttributes_->SetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, kFriendlyName);
    }
}

WebRtcBridgeMediaSource::~WebRtcBridgeMediaSource() {
    Shutdown();
    ModuleObjectDestroyed();
}

// IMFAttributes implementation
IFACEMETHODIMP WebRtcBridgeMediaSource::GetItem(REFGUID guidKey, PROPVARIANT* pValue) {
    return sourceAttributes_ ? sourceAttributes_->GetItem(guidKey, pValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::GetItemType(REFGUID guidKey, MF_ATTRIBUTE_TYPE* pType) {
    return sourceAttributes_ ? sourceAttributes_->GetItemType(guidKey, pType) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::CompareItem(REFGUID guidKey, REFPROPVARIANT Value, BOOL* pbResult) {
    return sourceAttributes_ ? sourceAttributes_->CompareItem(guidKey, Value, pbResult) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::Compare(IMFAttributes* pTheirs, MF_ATTRIBUTES_MATCH_TYPE MatchType, BOOL* pbResult) {
    return sourceAttributes_ ? sourceAttributes_->Compare(pTheirs, MatchType, pbResult) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::GetUINT32(REFGUID guidKey, UINT32* punValue) {
    return sourceAttributes_ ? sourceAttributes_->GetUINT32(guidKey, punValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::GetUINT64(REFGUID guidKey, UINT64* punValue) {
    return sourceAttributes_ ? sourceAttributes_->GetUINT64(guidKey, punValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::GetDouble(REFGUID guidKey, double* pfValue) {
    return sourceAttributes_ ? sourceAttributes_->GetDouble(guidKey, pfValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::GetGUID(REFGUID guidKey, GUID* pguidValue) {
    return sourceAttributes_ ? sourceAttributes_->GetGUID(guidKey, pguidValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::GetStringLength(REFGUID guidKey, UINT32* pcchLength) {
    return sourceAttributes_ ? sourceAttributes_->GetStringLength(guidKey, pcchLength) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::GetString(REFGUID guidKey, LPWSTR pwszValue, UINT32 cchBufSize, UINT32* pcchLength) {
    return sourceAttributes_ ? sourceAttributes_->GetString(guidKey, pwszValue, cchBufSize, pcchLength) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::GetAllocatedString(REFGUID guidKey, LPWSTR* ppwszValue, UINT32* pcchLength) {
    return sourceAttributes_ ? sourceAttributes_->GetAllocatedString(guidKey, ppwszValue, pcchLength) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::GetBlobSize(REFGUID guidKey, UINT32* pcbBlobSize) {
    return sourceAttributes_ ? sourceAttributes_->GetBlobSize(guidKey, pcbBlobSize) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::GetBlob(REFGUID guidKey, UINT8* pBuf, UINT32 cbBufSize, UINT32* pcbBlobSize) {
    return sourceAttributes_ ? sourceAttributes_->GetBlob(guidKey, pBuf, cbBufSize, pcbBlobSize) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::GetAllocatedBlob(REFGUID guidKey, UINT8** ppBuf, UINT32* pcbSize) {
    return sourceAttributes_ ? sourceAttributes_->GetAllocatedBlob(guidKey, ppBuf, pcbSize) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::GetUnknown(REFGUID guidKey, REFIID riid, LPVOID* ppv) {
    return sourceAttributes_ ? sourceAttributes_->GetUnknown(guidKey, riid, ppv) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::SetItem(REFGUID guidKey, REFPROPVARIANT Value) {
    return sourceAttributes_ ? sourceAttributes_->SetItem(guidKey, Value) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::DeleteItem(REFGUID guidKey) {
    return sourceAttributes_ ? sourceAttributes_->DeleteItem(guidKey) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::DeleteAllItems() {
    return sourceAttributes_ ? sourceAttributes_->DeleteAllItems() : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::SetUINT32(REFGUID guidKey, UINT32 unValue) {
    return sourceAttributes_ ? sourceAttributes_->SetUINT32(guidKey, unValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::SetUINT64(REFGUID guidKey, UINT64 unValue) {
    return sourceAttributes_ ? sourceAttributes_->SetUINT64(guidKey, unValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::SetDouble(REFGUID guidKey, double fValue) {
    return sourceAttributes_ ? sourceAttributes_->SetDouble(guidKey, fValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::SetGUID(REFGUID guidKey, REFGUID guidValue) {
    return sourceAttributes_ ? sourceAttributes_->SetGUID(guidKey, guidValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::SetString(REFGUID guidKey, LPCWSTR wszValue) {
    return sourceAttributes_ ? sourceAttributes_->SetString(guidKey, wszValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::SetBlob(REFGUID guidKey, const UINT8* pBuf, UINT32 cbBufSize) {
    return sourceAttributes_ ? sourceAttributes_->SetBlob(guidKey, pBuf, cbBufSize) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::SetUnknown(REFGUID guidKey, IUnknown* pUnknown) {
    return sourceAttributes_ ? sourceAttributes_->SetUnknown(guidKey, pUnknown) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::LockStore() {
    return sourceAttributes_ ? sourceAttributes_->LockStore() : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::UnlockStore() {
    return sourceAttributes_ ? sourceAttributes_->UnlockStore() : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::GetCount(UINT32* pcItems) {
    return sourceAttributes_ ? sourceAttributes_->GetCount(pcItems) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::GetItemByIndex(UINT32 unIndex, GUID* pguidKey, PROPVARIANT* pValue) {
    return sourceAttributes_ ? sourceAttributes_->GetItemByIndex(unIndex, pguidKey, pValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaSource::CopyAllItems(IMFAttributes* pDest) {
    return sourceAttributes_ ? sourceAttributes_->CopyAllItems(pDest) : E_UNEXPECTED;
}

HRESULT WebRtcBridgeMediaSource::Initialize() {
    std::lock_guard<std::mutex> lock(lock_);
    if (isInitialized_) return S_OK;

    if (!sourceAttributes_) {
        HRESULT hrAttr = MFCreateAttributes(&sourceAttributes_, 8);
        if (FAILED(hrAttr)) return hrAttr;
    }

    sourceAttributes_->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    sourceAttributes_->SetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, L"WebRTC Bridge Virtual Camera");
    sourceAttributes_->SetGUID(MFT_TRANSFORM_CLSID_Attribute, CLSID_WebRtcBridgeVirtualCameraMediaSource);

    // Sensor profile collection for Windows 11 Frame Server, Camera Settings & Windows Camera App
    Microsoft::WRL::ComPtr<IMFSensorProfileCollection> collection;
    HRESULT hr = MFCreateSensorProfileCollection(&collection);
    if (SUCCEEDED(hr)) {
        auto addProfile = [&](REFGUID profileId, LPCWSTR filter) {
            Microsoft::WRL::ComPtr<IMFSensorProfile> profile;
            if (SUCCEEDED(MFCreateSensorProfile(profileId, 0, nullptr, &profile))) {
                profile->AddProfileFilter(0, filter);
                collection->AddProfile(profile.Get());
            }
        };

        addProfile(KSCAMERAPROFILE_Legacy, L"((RES==;FRT<=30,1;SUT==))");
        addProfile(KSCAMERAPROFILE_VideoRecording, L"((RES==;FRT<=30,1;SUT==))");
        addProfile(KSCAMERAPROFILE_VideoConferencing, L"((RES==;FRT<=30,1;SUT==))");
        addProfile(KSCAMERAPROFILE_PhotoSequence, L"((RES==;FRT<=30,1;SUT==))");
        addProfile(KSCAMERAPROFILE_HighFrameRate, L"((RES==;FRT>=60,1;SUT==))");

        sourceAttributes_->SetUnknown(MF_DEVICEMFT_SENSORPROFILE_COLLECTION, collection.Get());
    }

    Microsoft::WRL::ComPtr<IMFStreamDescriptor> streamDesc;
    hr = CreateStreamDescriptor(&streamDesc);
    if (FAILED(hr)) return hr;

    IMFStreamDescriptor* streamDescs[1] = { streamDesc.Get() };
    hr = MFCreatePresentationDescriptor(1, streamDescs, &presentationDesc_);
    if (FAILED(hr)) return hr;

    hr = presentationDesc_->SelectStream(0);
    if (FAILED(hr)) return hr;

    stream_.Attach(new (std::nothrow) WebRtcBridgeMediaStream(this, streamDesc.Get()));
    if (!stream_) return E_OUTOFMEMORY;

    isInitialized_ = true;
    return S_OK;
}

HRESULT WebRtcBridgeMediaSource::CreateStreamDescriptor(IMFStreamDescriptor** ppDescriptor) {
    if (!ppDescriptor) return E_POINTER;
    *ppDescriptor = nullptr;

    auto createMediaType = [](GUID subType, UINT32 w, UINT32 h, UINT32 fps) -> Microsoft::WRL::ComPtr<IMFMediaType> {
        Microsoft::WRL::ComPtr<IMFMediaType> mt;
        if (FAILED(MFCreateMediaType(&mt))) return nullptr;
        mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        mt->SetGUID(MF_MT_SUBTYPE, subType);
        MFSetAttributeSize(mt.Get(), MF_MT_FRAME_SIZE, w, h);
        MFSetAttributeRatio(mt.Get(), MF_MT_FRAME_RATE, fps, 1);
        MFSetAttributeRatio(mt.Get(), MF_MT_FRAME_RATE_RANGE_MIN, 15, 1);
        MFSetAttributeRatio(mt.Get(), MF_MT_FRAME_RATE_RANGE_MAX, fps, 1);
        MFSetAttributeRatio(mt.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        mt->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        mt->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        mt->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
        DWORD sampleBytes = (subType == MFVideoFormat_RGB32) ? (w * h * 4) : (w * h * 3 / 2);
        mt->SetUINT32(MF_MT_SAMPLE_SIZE, sampleBytes);
        DWORD stride = (subType == MFVideoFormat_RGB32) ? (w * 4) : w;
        mt->SetUINT32(MF_MT_DEFAULT_STRIDE, stride);
        uint32_t bitrate = static_cast<uint32_t>(sampleBytes * 8 * fps);
        mt->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
        return mt;
    };

    // Format list: 120 FPS, 60 FPS, and 30 FPS for both NV12 and RGB32
    auto nv12_120 = createMediaType(MFVideoFormat_NV12, protocol::kWidth, protocol::kHeight, 120);
    auto nv12_60  = createMediaType(MFVideoFormat_NV12, protocol::kWidth, protocol::kHeight, 60);
    auto nv12_30  = createMediaType(MFVideoFormat_NV12, protocol::kWidth, protocol::kHeight, 30);
    auto rgb_120  = createMediaType(MFVideoFormat_RGB32, protocol::kWidth, protocol::kHeight, 120);
    auto rgb_60   = createMediaType(MFVideoFormat_RGB32, protocol::kWidth, protocol::kHeight, 60);
    auto rgb_30   = createMediaType(MFVideoFormat_RGB32, protocol::kWidth, protocol::kHeight, 30);

    IMFMediaType* mediaTypes[] = {
        nv12_60.Get(),
        nv12_120.Get(),
        nv12_30.Get(),
        rgb_60.Get(),
        rgb_120.Get(),
        rgb_30.Get()
    };
    const DWORD typeCount = sizeof(mediaTypes) / sizeof(mediaTypes[0]);

    Microsoft::WRL::ComPtr<IMFMediaTypeHandler> handler;
    Microsoft::WRL::ComPtr<IMFStreamDescriptor> streamDesc;

    HRESULT hr = MFCreateStreamDescriptor(0, typeCount, mediaTypes, &streamDesc);
    if (FAILED(hr)) return hr;

    // Set mandatory stream attributes required by FrameServer and Virtual Camera pipeline
    hr = streamDesc->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
    if (FAILED(hr)) return hr;

    hr = streamDesc->SetUINT32(MF_DEVICESTREAM_STREAM_ID, 0);
    if (FAILED(hr)) return hr;

    hr = streamDesc->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
    if (FAILED(hr)) return hr;

    hr = streamDesc->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes_Color);
    if (FAILED(hr)) return hr;

    hr = streamDesc->GetMediaTypeHandler(&handler);
    if (FAILED(hr)) return hr;

    hr = handler->SetCurrentMediaType(nv12_60.Get());
    if (FAILED(hr)) return hr;

    *ppDescriptor = streamDesc.Detach();
    return S_OK;
}

IFACEMETHODIMP WebRtcBridgeMediaSource::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    wchar_t szGuid[64] = {0};
    StringFromGUID2(riid, szGuid, 64);
    LogVcam(L"  [WebRtcBridgeMediaSource::QI] riid=%s", szGuid);

    if (riid == IID_IUnknown) {
        *ppv = static_cast<IUnknown*>(static_cast<IMFMediaSourceEx*>(this));
    } else if (riid == IID_IMFMediaEventGenerator) {
        *ppv = static_cast<IMFMediaEventGenerator*>(this);
    } else if (riid == IID_IMFMediaSource) {
        *ppv = static_cast<IMFMediaSource*>(this);
    } else if (riid == IID_IMFMediaSourceEx) {
        *ppv = static_cast<IMFMediaSourceEx*>(this);
    } else if (riid == IID_IMFAttributes) {
        *ppv = static_cast<IMFAttributes*>(this);
    } else if (riid == IID_IMFGetService) {
        *ppv = static_cast<IMFGetService*>(this);
    } else if (riid == IID_IMFSampleAllocatorControl) {
        *ppv = static_cast<IMFSampleAllocatorControl*>(this);
    } else if (riid == __uuidof(IKsControl)) {
        *ppv = static_cast<IKsControl*>(this);
    } else {
        LogVcam(L"    -> E_NOINTERFACE for riid=%s", szGuid);
        return E_NOINTERFACE;
    }

    LogVcam(L"    -> S_OK for riid=%s", szGuid);
    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) WebRtcBridgeMediaSource::AddRef() {
    return ++refCount_;
}

IFACEMETHODIMP_(ULONG) WebRtcBridgeMediaSource::Release() {
    ULONG count = --refCount_;
    if (count == 0) {
        delete this;
    }
    return count;
}

IFACEMETHODIMP WebRtcBridgeMediaSource::BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState) {
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;
    return eventQueue_ ? eventQueue_->BeginGetEvent(pCallback, punkState) : MF_E_SHUTDOWN;
}

IFACEMETHODIMP WebRtcBridgeMediaSource::EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent) {
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;
    return eventQueue_ ? eventQueue_->EndGetEvent(pResult, ppEvent) : MF_E_SHUTDOWN;
}

IFACEMETHODIMP WebRtcBridgeMediaSource::GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent) {
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> queue;
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (isShutdown_) return MF_E_SHUTDOWN;
        queue = eventQueue_;
    }
    // GetEvent may block. Holding the source lock here prevents Start/Stop
    // from queuing the event that wakes the caller.
    return queue ? queue->GetEvent(dwFlags, ppEvent) : MF_E_SHUTDOWN;
}

IFACEMETHODIMP WebRtcBridgeMediaSource::QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue) {
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;
    return eventQueue_ ? eventQueue_->QueueEventParamVar(met, guidExtendedType, hrStatus, pvValue) : MF_E_SHUTDOWN;
}

IFACEMETHODIMP WebRtcBridgeMediaSource::GetCharacteristics(DWORD* pdwCharacteristics) {
    if (!pdwCharacteristics) return E_POINTER;
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;

    // Live capture source characteristics
    *pdwCharacteristics = MFMEDIASOURCE_IS_LIVE;
    return S_OK;
}

IFACEMETHODIMP WebRtcBridgeMediaSource::CreatePresentationDescriptor(IMFPresentationDescriptor** ppPresentationDescriptor) {
    LogVcam(L"[WebRtcBridgeMediaSource::CreatePresentationDescriptor]");
    if (!ppPresentationDescriptor) return E_POINTER;
    *ppPresentationDescriptor = nullptr;

    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;
    if (!presentationDesc_) return E_UNEXPECTED;

    return presentationDesc_->Clone(ppPresentationDescriptor);
}

IFACEMETHODIMP WebRtcBridgeMediaSource::Start(
    IMFPresentationDescriptor* pPresentationDescriptor,
    const GUID* pguidTimeFormat,
    const PROPVARIANT* pvarStartPosition
) {
    LogVcam(L"[WebRtcBridgeMediaSource::Start]");
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;

    // Use the media type selected on the caller's presentation descriptor.
    // The descriptor owned by the source is cloned, so reading only the
    // source-side descriptor would silently keep the default subtype.
    if (stream_) {
        BOOL isSelected = TRUE;
        Microsoft::WRL::ComPtr<IMFStreamDescriptor> selectedDescriptor;
        if (pPresentationDescriptor) {
            HRESULT hrDescriptor = pPresentationDescriptor->GetStreamDescriptorByIndex(
                0, &isSelected, &selectedDescriptor);
            if (FAILED(hrDescriptor)) return hrDescriptor;
        }

        if (isSelected) {
            if (!selectedDescriptor) {
                HRESULT hrDescriptor = presentationDesc_->GetStreamDescriptorByIndex(
                    0, &isSelected, &selectedDescriptor);
                if (FAILED(hrDescriptor)) return hrDescriptor;
            }

            Microsoft::WRL::ComPtr<IMFMediaTypeHandler> handler;
            Microsoft::WRL::ComPtr<IMFMediaType> mediaType;
            HRESULT hrType = selectedDescriptor->GetMediaTypeHandler(&handler);
            if (FAILED(hrType)) return hrType;
            hrType = handler->GetCurrentMediaType(&mediaType);
            if (FAILED(hrType)) return hrType;
            hrType = stream_->SetMediaType(mediaType.Get());
            if (FAILED(hrType)) return hrType;

            HRESULT hrEvent = eventQueue_->QueueEventParamUnk(
                MENewStream, GUID_NULL, S_OK, static_cast<IMFMediaStream*>(stream_.Get()));
            if (FAILED(hrEvent)) return hrEvent;
            HRESULT hrStart = stream_->Start();
            if (FAILED(hrStart)) return hrStart;
        }
    }

    PROPVARIANT varStart;
    PropVariantInit(&varStart);
    if (pvarStartPosition) {
        PropVariantCopy(&varStart, pvarStartPosition);
    } else {
        varStart.vt = VT_EMPTY;
    }
    HRESULT hr = eventQueue_->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, &varStart);
    PropVariantClear(&varStart);
    return hr;
}

IFACEMETHODIMP WebRtcBridgeMediaSource::Stop() {
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;

    if (stream_) {
        stream_->Stop();
    }

    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_EMPTY;
    HRESULT hr = eventQueue_->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, &var);
    PropVariantClear(&var);
    return hr;
}

IFACEMETHODIMP WebRtcBridgeMediaSource::Pause() {
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;

    if (stream_) {
        stream_->Pause();
    }

    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_EMPTY;
    HRESULT hr = eventQueue_->QueueEventParamVar(MESourcePaused, GUID_NULL, S_OK, &var);
    PropVariantClear(&var);
    return hr;
}

IFACEMETHODIMP WebRtcBridgeMediaSource::Shutdown() {
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return S_OK;
    isShutdown_ = true;

    if (stream_) {
        stream_->Shutdown();
        stream_.Reset();
    }

    if (eventQueue_) {
        eventQueue_->Shutdown();
        eventQueue_.Reset();
    }

    presentationDesc_.Reset();
    return S_OK;
}

IFACEMETHODIMP WebRtcBridgeMediaSource::GetService(REFGUID guidService, REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    *ppvObject = nullptr;

    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;

    return QueryInterface(riid, ppvObject);
}

// IMFMediaSourceEx
IFACEMETHODIMP WebRtcBridgeMediaSource::GetSourceAttributes(IMFAttributes** ppAttributes) {
    if (!ppAttributes) return E_POINTER;
    *ppAttributes = nullptr;

    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;

    if (!sourceAttributes_) {
        HRESULT hr = MFCreateAttributes(&sourceAttributes_, 3);
        if (FAILED(hr)) return hr;
    }

    *ppAttributes = sourceAttributes_.Get();
    (*ppAttributes)->AddRef();
    return S_OK;
}

IFACEMETHODIMP WebRtcBridgeMediaSource::GetStreamAttributes(DWORD dwStreamIdentifier, IMFAttributes** ppAttributes) {
    if (!ppAttributes) return E_POINTER;
    *ppAttributes = nullptr;

    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;

    if (dwStreamIdentifier != 0) return MF_E_INVALIDSTREAMNUMBER;

    if (stream_) {
        return stream_->QueryInterface(IID_IMFAttributes, (void**)ppAttributes);
    }

    return MF_E_NOT_INITIALIZED;
}

IFACEMETHODIMP WebRtcBridgeMediaSource::SetD3DManager(IUnknown* pManager) {
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;
    dxgiManager_ = pManager;
    if (stream_ && pManager) {
        stream_->SetD3DManager(pManager);
    }
    LogVcam(L"[WebRtcBridgeMediaSource::SetD3DManager] accepted DXGI device manager");
    return S_OK;
}

// IMFSampleAllocatorControl
IFACEMETHODIMP WebRtcBridgeMediaSource::SetDefaultAllocator(DWORD dwOutputStreamID, IUnknown* pAllocator) {
    if (dwOutputStreamID != 0) return MF_E_INVALIDSTREAMNUMBER;
    if (!pAllocator) return E_POINTER;
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;
    return stream_ ? stream_->SetSampleAllocator(pAllocator) : E_UNEXPECTED;
}

IFACEMETHODIMP WebRtcBridgeMediaSource::GetAllocatorUsage(DWORD dwOutputStreamID, DWORD* pdwInputStreamID, MFSampleAllocatorUsage* peUsage) {
    if (dwOutputStreamID != 0) return MF_E_INVALIDSTREAMNUMBER;
    if (!pdwInputStreamID || !peUsage) return E_POINTER;
    *pdwInputStreamID = dwOutputStreamID;
    *peUsage = MFSampleAllocatorUsage_DoesNotAllocate;
    return S_OK;
}

// IKsControl
IFACEMETHODIMP WebRtcBridgeMediaSource::KsProperty(PKSPROPERTY Property, ULONG PropertyLength, LPVOID PropertyData, ULONG DataLength, ULONG* BytesReturned) {
    if (BytesReturned) *BytesReturned = 0;
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

IFACEMETHODIMP WebRtcBridgeMediaSource::KsMethod(PKSMETHOD Method, ULONG MethodLength, LPVOID MethodData, ULONG DataLength, ULONG* BytesReturned) {
    if (BytesReturned) *BytesReturned = 0;
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

IFACEMETHODIMP WebRtcBridgeMediaSource::KsEvent(PKSEVENT Event, ULONG EventLength, LPVOID EventData, ULONG DataLength, ULONG* BytesReturned) {
    if (BytesReturned) *BytesReturned = 0;
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

} // namespace km::vcam
