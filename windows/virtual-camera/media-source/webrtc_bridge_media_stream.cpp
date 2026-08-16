#include "webrtc_bridge_media_stream.h"
#include "webrtc_bridge_media_source.h"

namespace km::vcam {

WebRtcBridgeMediaStream::WebRtcBridgeMediaStream(
    WebRtcBridgeMediaSource* source,
    IMFStreamDescriptor* streamDesc,
    PipeFrameReceiver* receiver
) : source_(source), streamDesc_(streamDesc), frameReceiver_(receiver) {
    MFCreateEventQueue(&eventQueue_);
    frameScratch_.resize(protocol::kPayloadBytes);
}

WebRtcBridgeMediaStream::~WebRtcBridgeMediaStream() {
    Shutdown();
}

IFACEMETHODIMP WebRtcBridgeMediaStream::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    if (riid == IID_IUnknown) {
        *ppv = static_cast<IUnknown*>(static_cast<IMFMediaStream*>(this));
    } else if (riid == IID_IMFMediaEventGenerator) {
        *ppv = static_cast<IMFMediaEventGenerator*>(this);
    } else if (riid == IID_IMFMediaStream) {
        *ppv = static_cast<IMFMediaStream*>(this);
    } else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) WebRtcBridgeMediaStream::AddRef() {
    return ++refCount_;
}

IFACEMETHODIMP_(ULONG) WebRtcBridgeMediaStream::Release() {
    ULONG count = --refCount_;
    if (count == 0) {
        delete this;
    }
    return count;
}

IFACEMETHODIMP WebRtcBridgeMediaStream::BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState) {
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;
    return eventQueue_ ? eventQueue_->BeginGetEvent(pCallback, punkState) : MF_E_SHUTDOWN;
}

IFACEMETHODIMP WebRtcBridgeMediaStream::EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent) {
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;
    return eventQueue_ ? eventQueue_->EndGetEvent(pResult, ppEvent) : MF_E_SHUTDOWN;
}

IFACEMETHODIMP WebRtcBridgeMediaStream::GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent) {
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;
    return eventQueue_ ? eventQueue_->GetEvent(dwFlags, ppEvent) : MF_E_SHUTDOWN;
}

IFACEMETHODIMP WebRtcBridgeMediaStream::QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue) {
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;
    return eventQueue_ ? eventQueue_->QueueEventParamVar(met, guidExtendedType, hrStatus, pvValue) : MF_E_SHUTDOWN;
}

IFACEMETHODIMP WebRtcBridgeMediaStream::GetMediaSource(IMFMediaSource** ppMediaSource) {
    if (!ppMediaSource) return E_POINTER;
    *ppMediaSource = nullptr;

    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;
    if (!source_) return E_UNEXPECTED;

    *ppMediaSource = static_cast<IMFMediaSource*>(source_);
    (*ppMediaSource)->AddRef();
    return S_OK;
}

IFACEMETHODIMP WebRtcBridgeMediaStream::GetStreamDescriptor(IMFStreamDescriptor** ppStreamDescriptor) {
    if (!ppStreamDescriptor) return E_POINTER;
    *ppStreamDescriptor = nullptr;

    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;
    if (!streamDesc_) return E_UNEXPECTED;

    *ppStreamDescriptor = streamDesc_.Get();
    (*ppStreamDescriptor)->AddRef();
    return S_OK;
}

IFACEMETHODIMP WebRtcBridgeMediaStream::RequestSample(IUnknown* pToken) {
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;
    if (!isStarted_) return MF_E_INVALIDREQUEST;

    sampleRequests_.push(pToken);
    return DeliverSamples();
}

HRESULT WebRtcBridgeMediaStream::Start() {
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;

    isStarted_ = true;
    sampleIndex_ = 0;

    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_EMPTY;
    HRESULT hr = eventQueue_->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, &var);
    PropVariantClear(&var);

    DeliverSamples();
    return hr;
}

HRESULT WebRtcBridgeMediaStream::Stop() {
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;

    isStarted_ = false;
    while (!sampleRequests_.empty()) {
        sampleRequests_.pop();
    }

    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_EMPTY;
    HRESULT hr = eventQueue_->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, &var);
    PropVariantClear(&var);
    return hr;
}

HRESULT WebRtcBridgeMediaStream::Pause() {
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;

    isStarted_ = false;
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_EMPTY;
    HRESULT hr = eventQueue_->QueueEventParamVar(MEStreamPaused, GUID_NULL, S_OK, &var);
    PropVariantClear(&var);
    return hr;
}

HRESULT WebRtcBridgeMediaStream::Shutdown() {
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return S_OK;
    isShutdown_ = true;
    isStarted_ = false;

    while (!sampleRequests_.empty()) {
        sampleRequests_.pop();
    }

    if (eventQueue_) {
        eventQueue_->Shutdown();
        eventQueue_.Reset();
    }
    streamDesc_.Reset();
    source_ = nullptr;
    return S_OK;
}

HRESULT WebRtcBridgeMediaStream::DeliverSamples() {
    while (!sampleRequests_.empty()) {
        Microsoft::WRL::ComPtr<IUnknown> token = sampleRequests_.front();
        sampleRequests_.pop();

        Microsoft::WRL::ComPtr<IMFSample> sample;
        HRESULT hr = CreateVideoSample(&sample);
        if (FAILED(hr)) {
            return hr;
        }

        if (token) {
            sample->SetUnknown(MFSampleExtension_Token, token.Get());
        }

        hr = eventQueue_->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample.Get());
        if (FAILED(hr)) {
            return hr;
        }
    }
    return S_OK;
}

HRESULT WebRtcBridgeMediaStream::CreateVideoSample(IMFSample** ppSample) {
    if (!ppSample) return E_POINTER;
    *ppSample = nullptr;

    uint64_t seq = 0;
    int64_t tsUs = 0;
    if (frameReceiver_) {
        frameReceiver_->GetLatestFrame(frameScratch_, seq, tsUs);
    } else {
        protocol::FillBlackNv12(frameScratch_);
    }

    Microsoft::WRL::ComPtr<IMFMediaBuffer> mediaBuffer;
    HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(protocol::kPayloadBytes), &mediaBuffer);
    if (FAILED(hr)) return hr;

    BYTE* pDst = nullptr;
    DWORD maxLen = 0, curLen = 0;
    hr = mediaBuffer->Lock(&pDst, &maxLen, &curLen);
    if (FAILED(hr)) return hr;

    std::memcpy(pDst, frameScratch_.data(), protocol::kPayloadBytes);
    mediaBuffer->Unlock();
    mediaBuffer->SetCurrentLength(static_cast<DWORD>(protocol::kPayloadBytes));

    Microsoft::WRL::ComPtr<IMFSample> sample;
    hr = MFCreateSample(&sample);
    if (FAILED(hr)) return hr;

    hr = sample->AddBuffer(mediaBuffer.Get());
    if (FAILED(hr)) return hr;

    // Rational 30fps presentation time: 100ns units (10,000,000 / 30)
    LONGLONG sampleTime = (static_cast<LONGLONG>(sampleIndex_) * 10000000LL) / 30LL;
    LONGLONG nextSampleTime = (static_cast<LONGLONG>(sampleIndex_ + 1) * 10000000LL) / 30LL;
    LONGLONG sampleDuration = nextSampleTime - sampleTime;
    sampleIndex_++;

    sample->SetSampleTime(sampleTime);
    sample->SetSampleDuration(sampleDuration);

    *ppSample = sample.Detach();
    return S_OK;
}

} // namespace km::vcam
