#include "webrtc_bridge_media_stream.h"
#include "webrtc_bridge_media_source.h"
#include "vcam_logger.h"
#include <timeapi.h>
#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace km::vcam {

WebRtcBridgeMediaStream::WebRtcBridgeMediaStream(
    WebRtcBridgeMediaSource* source,
    IMFStreamDescriptor* streamDesc
) : source_(source), streamDesc_(streamDesc) {
    hWakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    hSampleRequestedEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    MFCreateEventQueue(&eventQueue_);
    MFCreateAttributes(&streamAttributes_, 8);
    if (streamAttributes_) {
        streamAttributes_->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
        streamAttributes_->SetUINT32(MF_DEVICESTREAM_STREAM_ID, 0);
        streamAttributes_->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
        streamAttributes_->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes_Color);
    }
    frameScratch_.resize(protocol::kPayloadBytes);
}

WebRtcBridgeMediaStream::~WebRtcBridgeMediaStream() {
    Shutdown();
    if (hWakeEvent_) {
        CloseHandle(hWakeEvent_);
        hWakeEvent_ = nullptr;
    }
    if (hSampleRequestedEvent_) {
        CloseHandle(hSampleRequestedEvent_);
        hSampleRequestedEvent_ = nullptr;
    }
}

// IMFAttributes implementation
IFACEMETHODIMP WebRtcBridgeMediaStream::GetItem(REFGUID guidKey, PROPVARIANT* pValue) {
    return streamAttributes_ ? streamAttributes_->GetItem(guidKey, pValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::GetItemType(REFGUID guidKey, MF_ATTRIBUTE_TYPE* pType) {
    return streamAttributes_ ? streamAttributes_->GetItemType(guidKey, pType) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::CompareItem(REFGUID guidKey, REFPROPVARIANT Value, BOOL* pbResult) {
    return streamAttributes_ ? streamAttributes_->CompareItem(guidKey, Value, pbResult) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::Compare(IMFAttributes* pTheirs, MF_ATTRIBUTES_MATCH_TYPE MatchType, BOOL* pbResult) {
    return streamAttributes_ ? streamAttributes_->Compare(pTheirs, MatchType, pbResult) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::GetUINT32(REFGUID guidKey, UINT32* punValue) {
    return streamAttributes_ ? streamAttributes_->GetUINT32(guidKey, punValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::GetUINT64(REFGUID guidKey, UINT64* punValue) {
    return streamAttributes_ ? streamAttributes_->GetUINT64(guidKey, punValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::GetDouble(REFGUID guidKey, double* pfValue) {
    return streamAttributes_ ? streamAttributes_->GetDouble(guidKey, pfValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::GetGUID(REFGUID guidKey, GUID* pguidValue) {
    return streamAttributes_ ? streamAttributes_->GetGUID(guidKey, pguidValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::GetStringLength(REFGUID guidKey, UINT32* pcchLength) {
    return streamAttributes_ ? streamAttributes_->GetStringLength(guidKey, pcchLength) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::GetString(REFGUID guidKey, LPWSTR pwszValue, UINT32 cchBufSize, UINT32* pcchLength) {
    return streamAttributes_ ? streamAttributes_->GetString(guidKey, pwszValue, cchBufSize, pcchLength) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::GetAllocatedString(REFGUID guidKey, LPWSTR* ppwszValue, UINT32* pcchLength) {
    return streamAttributes_ ? streamAttributes_->GetAllocatedString(guidKey, ppwszValue, pcchLength) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::GetBlobSize(REFGUID guidKey, UINT32* pcbBlobSize) {
    return streamAttributes_ ? streamAttributes_->GetBlobSize(guidKey, pcbBlobSize) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::GetBlob(REFGUID guidKey, UINT8* pBuf, UINT32 cbBufSize, UINT32* pcbBlobSize) {
    return streamAttributes_ ? streamAttributes_->GetBlob(guidKey, pBuf, cbBufSize, pcbBlobSize) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::GetAllocatedBlob(REFGUID guidKey, UINT8** ppBuf, UINT32* pcbSize) {
    return streamAttributes_ ? streamAttributes_->GetAllocatedBlob(guidKey, ppBuf, pcbSize) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::GetUnknown(REFGUID guidKey, REFIID riid, LPVOID* ppv) {
    return streamAttributes_ ? streamAttributes_->GetUnknown(guidKey, riid, ppv) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::SetItem(REFGUID guidKey, REFPROPVARIANT Value) {
    return streamAttributes_ ? streamAttributes_->SetItem(guidKey, Value) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::DeleteItem(REFGUID guidKey) {
    return streamAttributes_ ? streamAttributes_->DeleteItem(guidKey) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::DeleteAllItems() {
    return streamAttributes_ ? streamAttributes_->DeleteAllItems() : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::SetUINT32(REFGUID guidKey, UINT32 unValue) {
    return streamAttributes_ ? streamAttributes_->SetUINT32(guidKey, unValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::SetUINT64(REFGUID guidKey, UINT64 unValue) {
    return streamAttributes_ ? streamAttributes_->SetUINT64(guidKey, unValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::SetDouble(REFGUID guidKey, double fValue) {
    return streamAttributes_ ? streamAttributes_->SetDouble(guidKey, fValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::SetGUID(REFGUID guidKey, REFGUID guidValue) {
    return streamAttributes_ ? streamAttributes_->SetGUID(guidKey, guidValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::SetString(REFGUID guidKey, LPCWSTR wszValue) {
    return streamAttributes_ ? streamAttributes_->SetString(guidKey, wszValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::SetBlob(REFGUID guidKey, const UINT8* pBuf, UINT32 cbBufSize) {
    return streamAttributes_ ? streamAttributes_->SetBlob(guidKey, pBuf, cbBufSize) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::SetUnknown(REFGUID guidKey, IUnknown* pUnknown) {
    return streamAttributes_ ? streamAttributes_->SetUnknown(guidKey, pUnknown) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::LockStore() {
    return streamAttributes_ ? streamAttributes_->LockStore() : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::UnlockStore() {
    return streamAttributes_ ? streamAttributes_->UnlockStore() : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::GetCount(UINT32* pcItems) {
    return streamAttributes_ ? streamAttributes_->GetCount(pcItems) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::GetItemByIndex(UINT32 unIndex, GUID* pguidKey, PROPVARIANT* pValue) {
    return streamAttributes_ ? streamAttributes_->GetItemByIndex(unIndex, pguidKey, pValue) : E_UNEXPECTED;
}
IFACEMETHODIMP WebRtcBridgeMediaStream::CopyAllItems(IMFAttributes* pDest) {
    return streamAttributes_ ? streamAttributes_->CopyAllItems(pDest) : E_UNEXPECTED;
}

// IMFGetService
IFACEMETHODIMP WebRtcBridgeMediaStream::GetService(REFGUID guidService, REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    *ppvObject = nullptr;
    return QueryInterface(riid, ppvObject);
}

IFACEMETHODIMP WebRtcBridgeMediaStream::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    if (riid == IID_IUnknown) {
        *ppv = static_cast<IUnknown*>(static_cast<IMFMediaStream2*>(this));
    } else if (riid == IID_IMFMediaEventGenerator) {
        *ppv = static_cast<IMFMediaEventGenerator*>(this);
    } else if (riid == IID_IMFMediaStream || riid == IID_IMFMediaStream2) {
        *ppv = static_cast<IMFMediaStream2*>(this);
    } else if (riid == IID_IMFAttributes) {
        *ppv = static_cast<IMFAttributes*>(this);
    } else if (riid == IID_IMFGetService) {
        *ppv = static_cast<IMFGetService*>(this);
    } else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

IFACEMETHODIMP WebRtcBridgeMediaStream::SetStreamState(MF_STREAM_STATE value) {
    LogVcam(L"[WebRtcBridgeMediaStream::SetStreamState] value=%d", (int)value);
    {
        std::lock_guard<std::mutex> lock(lock_);
        streamState_ = value;
        if (value == MF_STREAM_STATE_RUNNING) {
            isStarted_ = true;
            sampleIndex_ = 0;
            startTime_ = MFGetSystemTime();

            PROPVARIANT var;
            PropVariantInit(&var);
            var.vt = VT_EMPTY;
            if (eventQueue_) {
                eventQueue_->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, &var);
            }
            PropVariantClear(&var);
        } else if (value == MF_STREAM_STATE_PAUSED) {
            isStarted_ = false;
            PROPVARIANT var;
            PropVariantInit(&var);
            var.vt = VT_EMPTY;
            if (eventQueue_) {
                eventQueue_->QueueEventParamVar(MEStreamPaused, GUID_NULL, S_OK, &var);
            }
            PropVariantClear(&var);
        } else if (value == MF_STREAM_STATE_STOPPED) {
            isStarted_ = false;
            while (!sampleRequests_.empty()) {
                sampleRequests_.pop();
            }
            PROPVARIANT var;
            PropVariantInit(&var);
            var.vt = VT_EMPTY;
            if (eventQueue_) {
                eventQueue_->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, &var);
            }
            PropVariantClear(&var);
        }
    }

    if (value == MF_STREAM_STATE_RUNNING) {
        if (!isDeliveryRunning_.exchange(true)) {
            deliveryThread_ = std::thread(&WebRtcBridgeMediaStream::DeliveryThreadProc, this);
        }
    } else if (value == MF_STREAM_STATE_STOPPED || value == MF_STREAM_STATE_PAUSED) {
        isDeliveryRunning_ = false;
        if (deliveryThread_.joinable()) {
            deliveryThread_.join();
        }
    }
    return S_OK;
}

IFACEMETHODIMP WebRtcBridgeMediaStream::GetStreamState(MF_STREAM_STATE* value) {
    if (!value) return E_POINTER;
    std::lock_guard<std::mutex> lock(lock_);
    *value = streamState_;
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
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> queue;
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (isShutdown_) return MF_E_SHUTDOWN;
        queue = eventQueue_;
    }
    return queue ? queue->GetEvent(dwFlags, ppEvent) : MF_E_SHUTDOWN;
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
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (isShutdown_) return MF_E_SHUTDOWN;
        if (!isStarted_) return MF_E_INVALIDREQUEST;
        sampleRequests_.push(pToken);
    }
    if (hSampleRequestedEvent_) {
        SetEvent(hSampleRequestedEvent_);
    }
    return S_OK;
}

HRESULT WebRtcBridgeMediaStream::Start() {
    LogVcam(L"[WebRtcBridgeMediaStream::Start]");
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (isShutdown_) return MF_E_SHUTDOWN;

        isStarted_ = true;
        sampleIndex_ = 0;
        lastSampleTime_ = 0;
        startTime_ = MFGetSystemTime();

        PROPVARIANT var;
        PropVariantInit(&var);
        var.vt = VT_EMPTY;
        HRESULT hr = eventQueue_->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, &var);
        PropVariantClear(&var);
        if (FAILED(hr)) return hr;
    }

    if (!isDeliveryRunning_.exchange(true)) {
        deliveryThread_ = std::thread(&WebRtcBridgeMediaStream::DeliveryThreadProc, this);
    }
    return S_OK;
}

HRESULT WebRtcBridgeMediaStream::Stop() {
    LogVcam(L"[WebRtcBridgeMediaStream::Stop]");
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (isShutdown_) return MF_E_SHUTDOWN;
        isStarted_ = false;
        while (!sampleRequests_.empty()) {
            sampleRequests_.pop();
        }
    }

    isDeliveryRunning_ = false;
    SetEvent(hWakeEvent_);
    if (deliveryThread_.joinable()) {
        deliveryThread_.join();
    }

    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_EMPTY;
    HRESULT hr = eventQueue_->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, &var);
    PropVariantClear(&var);
    return hr;
}

HRESULT WebRtcBridgeMediaStream::Pause() {
    LogVcam(L"[WebRtcBridgeMediaStream::Pause]");
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
    LogVcam(L"[WebRtcBridgeMediaStream::Shutdown]");
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (isShutdown_) return S_OK;
        isShutdown_ = true;
        isStarted_ = false;
        while (!sampleRequests_.empty()) {
            sampleRequests_.pop();
        }
    }

    isDeliveryRunning_ = false;
    SetEvent(hWakeEvent_);
    if (deliveryThread_.joinable()) {
        deliveryThread_.join();
    }

    std::lock_guard<std::mutex> lock(lock_);
    if (eventQueue_) {
        eventQueue_->Shutdown();
        eventQueue_.Reset();
    }
    streamDesc_.Reset();
    source_ = nullptr;
    return S_OK;
}

void WebRtcBridgeMediaStream::DeliveryThreadProc() {
    timeBeginPeriod(1);
    HANDLE hNewFrame = shmConsumer_.GetEventHandle();
    while (isDeliveryRunning_) {
        DWORD waitRes = WAIT_TIMEOUT;
        if (hNewFrame) {
            HANDLE waitHandles[2] = { hWakeEvent_, hNewFrame };
            waitRes = WaitForMultipleObjects(2, waitHandles, FALSE, 33);
        } else {
            waitRes = WaitForSingleObject(hWakeEvent_, 33);
        }

        if (!isDeliveryRunning_) break;
        if (waitRes == WAIT_OBJECT_0) break; // hWakeEvent_ stop signaled

        DeliverSamples();
    }
    timeEndPeriod(1);
}

HRESULT WebRtcBridgeMediaStream::DeliverSamples() {
    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_ || !isStarted_) return S_OK;

    if (!sampleRequests_.empty()) {
        Microsoft::WRL::ComPtr<IUnknown> token = sampleRequests_.front();
        sampleRequests_.pop();

        Microsoft::WRL::ComPtr<IMFSample> sample;
        HRESULT hr = CreateVideoSample(&sample);
        if (FAILED(hr) || !sample) {
            LogVcam(L"  CreateVideoSample failed: hr=0x%08X", hr);
            return hr;
        }

        if (token) {
            sample->SetUnknown(MFSampleExtension_Token, token.Get());
        }

        hr = eventQueue_->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample.Get());
        if (FAILED(hr)) {
            LogVcam(L"  QueueEventParamUnk(MEMediaSample) failed: hr=0x%08X", hr);
            return hr;
        }
        if (sampleIndex_ <= 3 || sampleIndex_ % 300 == 0) {
            LogVcam(L"  [WebRtcBridgeMediaStream] Delivered Frame idx=%I64d", sampleIndex_);
        }
    }
    return S_OK;
}

HRESULT WebRtcBridgeMediaStream::SetMediaType(IMFMediaType* pMediaType) {
    if (!pMediaType) return E_POINTER;
    GUID subType = GUID_NULL;
    HRESULT hr = pMediaType->GetGUID(MF_MT_SUBTYPE, &subType);
    if (FAILED(hr)) return hr;

    UINT32 width = 0;
    UINT32 height = 0;
    hr = MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, &width, &height);
    if (FAILED(hr)) return hr;
    if ((subType != MFVideoFormat_NV12 && subType != MFVideoFormat_RGB32) ||
        width != protocol::kWidth || height != protocol::kHeight) {
        return MF_E_INVALIDMEDIATYPE;
    }

    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;
    currentSubType_ = subType;
    LogVcam(L"[WebRtcBridgeMediaStream::SetMediaType] subType=%s, size=%ux%u",
        (subType == MFVideoFormat_RGB32) ? L"RGB32" : L"NV12", width, height);
    return S_OK;
}

HRESULT WebRtcBridgeMediaStream::SetSampleAllocator(IUnknown* pAllocator) {
    if (!pAllocator) return E_POINTER;

    Microsoft::WRL::ComPtr<IMFVideoSampleAllocator> allocator;
    HRESULT hr = pAllocator->QueryInterface(IID_PPV_ARGS(&allocator));
    if (FAILED(hr)) return hr;

    std::lock_guard<std::mutex> lock(lock_);
    if (isShutdown_) return MF_E_SHUTDOWN;
    if (isStarted_) return MF_E_INVALIDREQUEST;
    sampleAllocator_ = allocator;
    LogVcam(L"[WebRtcBridgeMediaStream::SetSampleAllocator] camera pipeline allocator accepted");
    return S_OK;
}

HRESULT WebRtcBridgeMediaStream::CreateVideoSample(IMFSample** ppSample) {
    if (!ppSample) return E_POINTER;
    *ppSample = nullptr;

    uint64_t seq = 0;
    int64_t tsUs = 0;
    const uint8_t* pSrcNv12 = nullptr;
    bool hasFrame = shmConsumer_.GetLatestFrameDirect(pSrcNv12, seq, tsUs);
    if (!hasFrame || !pSrcNv12) {
        // Continuous monotonic time-based fallback frame index (cannot jump backward)
        uint64_t fallbackIndex = static_cast<uint64_t>((MFGetSystemTime() * 30) / 10000000);
        fallbackPatternGen_.GenerateFrame(frameScratch_, fallbackIndex, MFGetSystemTime() / 10);
        pSrcNv12 = frameScratch_.data();
    }

    if (sampleIndex_ < 3 || sampleIndex_ % 300 == 0) {
        LogVcam(L"[WebRtcBridgeMediaStream::CreateVideoSample] hasFrame=%d, seq=%I64d, subType=%s, allocator=%s",
            hasFrame ? 1 : 0, seq, (currentSubType_ == MFVideoFormat_RGB32) ? L"RGB32" : L"NV12",
            sampleAllocator_ ? L"pipeline" : L"fallback");
    }

    const size_t outBytes = (currentSubType_ == MFVideoFormat_RGB32)
        ? (protocol::kWidth * protocol::kHeight * 4)
        : protocol::kPayloadBytes;

    Microsoft::WRL::ComPtr<IMFSample> sample;
    Microsoft::WRL::ComPtr<IMFMediaBuffer> mediaBuffer;
    const bool usingPipelineAllocator = sampleAllocator_ != nullptr;

    HRESULT hr = S_OK;
    if (usingPipelineAllocator) {
        hr = sampleAllocator_->AllocateSample(&sample);
        if (FAILED(hr)) return hr;
        hr = sample->GetBufferByIndex(0, &mediaBuffer);
        if (FAILED(hr)) return hr;
    } else {
        hr = MFCreateMemoryBuffer(static_cast<DWORD>(outBytes), &mediaBuffer);
        if (FAILED(hr)) return hr;
        hr = MFCreateSample(&sample);
        if (FAILED(hr)) return hr;
        hr = sample->AddBuffer(mediaBuffer.Get());
        if (FAILED(hr)) return hr;
    }

    auto writeRows = [&](BYTE* firstRow, LONG pitch) -> HRESULT {
        const DWORD rowBytes = (currentSubType_ == MFVideoFormat_RGB32)
            ? protocol::kWidth * 4
            : protocol::kWidth;
        if (!firstRow || pitch == 0 || static_cast<DWORD>(std::abs(pitch)) < rowBytes) {
            return MF_E_BUFFERTOOSMALL;
        }

        if (currentSubType_ == MFVideoFormat_NV12) {
            const uint8_t* srcY = pSrcNv12;
            const uint8_t* srcUv = srcY + protocol::kWidth * protocol::kHeight;
            for (UINT32 y = 0; y < protocol::kHeight; ++y) {
                std::memcpy(firstRow + static_cast<ptrdiff_t>(y) * pitch,
                            srcY + static_cast<size_t>(y) * protocol::kWidth,
                            protocol::kWidth);
            }
            BYTE* firstUvRow = firstRow + static_cast<ptrdiff_t>(protocol::kHeight) * pitch;
            for (UINT32 y = 0; y < protocol::kHeight / 2; ++y) {
                std::memcpy(firstUvRow + static_cast<ptrdiff_t>(y) * pitch,
                            srcUv + static_cast<size_t>(y) * protocol::kWidth,
                            protocol::kWidth);
            }
            return S_OK;
        }

        // Convert studio-range BT.709 NV12 to top-down BGRA. Respecting the
        // allocator's pitch is essential for D3D-backed Frame Server buffers.
        const uint8_t* yPlane = pSrcNv12;
        const uint8_t* uvPlane = pSrcNv12 + (protocol::kWidth * protocol::kHeight);
        for (UINT32 y = 0; y < protocol::kHeight; ++y) {
            BYTE* dstRow = firstRow + static_cast<ptrdiff_t>(y) * pitch;
            for (UINT32 x = 0; x < protocol::kWidth; ++x) {
                int yVal = (std::max)(0, static_cast<int>(yPlane[y * protocol::kWidth + x]) - 16);
                int uVal = uvPlane[(y / 2) * protocol::kWidth + (x / 2) * 2] - 128;
                int vVal = uvPlane[(y / 2) * protocol::kWidth + (x / 2) * 2 + 1] - 128;

                int r = (298 * yVal + 459 * vVal + 128) >> 8;
                int g = (298 * yVal - 55 * uVal - 136 * vVal + 128) >> 8;
                int b = (298 * yVal + 541 * uVal + 128) >> 8;

                size_t dstIdx = static_cast<size_t>(x) * 4;
                dstRow[dstIdx + 0] = static_cast<BYTE>(std::clamp(b, 0, 255));
                dstRow[dstIdx + 1] = static_cast<BYTE>(std::clamp(g, 0, 255));
                dstRow[dstIdx + 2] = static_cast<BYTE>(std::clamp(r, 0, 255));
                dstRow[dstIdx + 3] = 0xFF;
            }
        }
        return S_OK;
    };

    Microsoft::WRL::ComPtr<IMF2DBuffer2> buffer2d;
    if (SUCCEEDED(mediaBuffer.As(&buffer2d))) {
        BYTE* scanline0 = nullptr;
        BYTE* bufferStart = nullptr;
        LONG pitch = 0;
        DWORD bufferLength = 0;
        hr = buffer2d->Lock2DSize(MF2DBuffer_LockFlags_Write, &scanline0, &pitch,
                                  &bufferStart, &bufferLength);
        if (FAILED(hr)) return hr;
        hr = writeRows(scanline0, pitch);
        HRESULT unlockHr = buffer2d->Unlock2D();
        if (FAILED(hr)) return hr;
        if (FAILED(unlockHr)) return unlockHr;
    } else {
        BYTE* destination = nullptr;
        DWORD maxLength = 0;
        DWORD currentLength = 0;
        hr = mediaBuffer->Lock(&destination, &maxLength, &currentLength);
        if (FAILED(hr)) return hr;
        if (maxLength < outBytes) {
            mediaBuffer->Unlock();
            return MF_E_BUFFERTOOSMALL;
        }
        hr = writeRows(destination,
                       currentSubType_ == MFVideoFormat_RGB32
                           ? static_cast<LONG>(protocol::kWidth * 4)
                           : static_cast<LONG>(protocol::kWidth));
        HRESULT unlockHr = mediaBuffer->Unlock();
        if (FAILED(hr)) return hr;
        if (FAILED(unlockHr)) return unlockHr;
    }

    mediaBuffer->SetCurrentLength(static_cast<DWORD>(outBytes));

    // A live video source timestamps each frame with the actual system capture time.
    LONGLONG sampleDuration = 10000000LL / 30LL;
    LONGLONG sampleTime = MFGetSystemTime();
    sampleIndex_++;

    sample->SetSampleTime(sampleTime);
    sample->SetSampleDuration(sampleDuration);
    sample->SetUINT32(MFSampleExtension_CleanPoint, 1);
    sample->SetUINT32(MFSampleExtension_Discontinuity, sampleIndex_ == 1 ? 1 : 0);

    *ppSample = sample.Detach();
    return S_OK;
}

} // namespace km::vcam
