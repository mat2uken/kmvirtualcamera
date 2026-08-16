#include "webrtc_bridge_media_source.h"

namespace km::vcam {

HRESULT WebRtcBridgeMediaSource::CreateInstance(IMFMediaSource** ppSource) {
    if (!ppSource) return E_POINTER;
    *ppSource = nullptr;

    WebRtcBridgeMediaSource* pSource = new (std::nothrow) WebRtcBridgeMediaSource();
    if (!pSource) return E_OUTOFMEMORY;

    HRESULT hr = pSource->Initialize();
    if (FAILED(hr)) {
        pSource->Release();
        return hr;
    }

    *ppSource = pSource;
    return S_OK;
}

WebRtcBridgeMediaSource::WebRtcBridgeMediaSource() {
    MFCreateEventQueue(&eventQueue_);
}

WebRtcBridgeMediaSource::~WebRtcBridgeMediaSource() {
    Shutdown();
}

HRESULT WebRtcBridgeMediaSource::Initialize() {
    std::lock_guard<std::mutex> lock(lock_);
    if (isInitialized_) return S_OK;

    Microsoft::WRL::ComPtr<IMFStreamDescriptor> streamDesc;
    HRESULT hr = CreateStreamDescriptor(&streamDesc);
    if (FAILED(hr)) return hr;

    IMFStreamDescriptor* streamDescs[1] = { streamDesc.Get() };
    hr = MFCreatePresentationDescriptor(1, streamDescs, &presentationDesc_);
    if (FAILED(hr)) return hr;

    hr = presentationDesc_->SelectStream(0);
    if (FAILED(hr)) return hr;

    stream_.Attach(new (std::nothrow) WebRtcBridgeMediaStream(this, streamDesc.Get(), &frameReceiver_));
    if (!stream_) return E_OUTOFMEMORY;

    isInitialized_ = true;
    return S_OK;
}

HRESULT WebRtcBridgeMediaSource::CreateStreamDescriptor(IMFStreamDescriptor** ppDescriptor) {
    if (!ppDescriptor) return E_POINTER;
    *ppDescriptor = nullptr;

    Microsoft::WRL::ComPtr<IMFMediaType> mediaType;
    HRESULT hr = MFCreateMediaType(&mediaType);
    if (FAILED(hr)) return hr;

    hr = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(hr)) return hr;

    hr = mediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    if (FAILED(hr)) return hr;

    hr = MFSetAttributeSize(mediaType.Get(), MF_MT_FRAME_SIZE, protocol::kWidth, protocol::kHeight);
    if (FAILED(hr)) return hr;

    hr = MFSetAttributeRatio(mediaType.Get(), MF_MT_FRAME_RATE, 30, 1);
    if (FAILED(hr)) return hr;

    hr = MFSetAttributeRatio(mediaType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(hr)) return hr;

    hr = mediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (FAILED(hr)) return hr;

    hr = mediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    if (FAILED(hr)) return hr;

    IMFMediaType* mediaTypes[1] = { mediaType.Get() };
    Microsoft::WRL::ComPtr<IMFMediaTypeHandler> handler;
    Microsoft::WRL::ComPtr<IMFStreamDescriptor> streamDesc;

    hr = MFCreateStreamDescriptor(0, 1, mediaTypes, &streamDesc);
    if (FAILED(hr)) return hr;

    hr = streamDesc->GetMediaTypeHandler(&handler);
    if (FAILED(hr)) return hr;

    hr = handler->SetCurrentMediaType(mediaType.Get());
    if (FAILED(hr)) return hr;

    *ppDescriptor = streamDesc.Detach();
    return S_OK;
}

IFACEMETHODIMP WebRtcBridgeMediaSource::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    if (riid == IID_IUnknown) {
        *ppv = static_cast<IUnknown*>(static_cast<IMFMediaSource*>(this));
    } else if (riid == IID_IMFMediaEventGenerator) {
        *ppv = static_cast<IMFMediaEventGenerator*>(this);
    } else if (riid == IID_IMFMediaSource) {
        *ppv = static_cast<IMFMediaSource*>(this);
    } else if (riid == IID_IMFGetService) {
        *ppv = static_cast<IMFGetService*>(this);
    } else {
        return E_NOINTERFACE;
    }

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
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;
    return eventQueue_ ? eventQueue_->GetEvent(dwFlags, ppEvent) : MF_E_SHUTDOWN;
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
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;

    frameReceiver_.Start();
    if (stream_) {
        stream_->Start();
    }

    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_EMPTY;
    HRESULT hr = eventQueue_->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, &var);
    PropVariantClear(&var);
    return hr;
}

IFACEMETHODIMP WebRtcBridgeMediaSource::Stop() {
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;

    if (stream_) {
        stream_->Stop();
    }
    frameReceiver_.Stop();

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

    frameReceiver_.Stop();

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

    if (guidService == MF_MEDIASOURCE_SERVICE) {
        return QueryInterface(riid, ppvObject);
    }

    return MF_E_UNSUPPORTED_SERVICE;
}

} // namespace km::vcam
