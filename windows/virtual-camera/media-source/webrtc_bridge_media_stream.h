#pragma once

#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mferror.h>
#include <wrl/client.h>
#include <mutex>
#include <queue>
#include <atomic>
#include "pipe_frame_receiver.h"

namespace km::vcam {

class WebRtcBridgeMediaSource;

class WebRtcBridgeMediaStream : public IMFMediaStream {
public:
    WebRtcBridgeMediaStream(WebRtcBridgeMediaSource* source, IMFStreamDescriptor* streamDesc, PipeFrameReceiver* receiver);
    virtual ~WebRtcBridgeMediaStream();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IMFMediaEventGenerator
    IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState) override;
    IFACEMETHODIMP EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent) override;
    IFACEMETHODIMP GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent) override;
    IFACEMETHODIMP QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue) override;

    // IMFMediaStream
    IFACEMETHODIMP GetMediaSource(IMFMediaSource** ppMediaSource) override;
    IFACEMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** ppStreamDescriptor) override;
    IFACEMETHODIMP RequestSample(IUnknown* pToken) override;

    // Internal controls from MediaSource
    HRESULT Start();
    HRESULT Stop();
    HRESULT Pause();
    HRESULT Shutdown();

    // Work queue callback for delivering samples
    HRESULT DeliverSamples();

private:
    HRESULT CreateVideoSample(IMFSample** ppSample);

    std::atomic<ULONG> refCount_{1};
    WebRtcBridgeMediaSource* source_{nullptr}; // Weak ref
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> eventQueue_;
    Microsoft::WRL::ComPtr<IMFStreamDescriptor> streamDesc_;
    PipeFrameReceiver* frameReceiver_{nullptr};

    std::mutex lock_;
    bool isStarted_{false};
    bool isShutdown_{false};
    std::queue<Microsoft::WRL::ComPtr<IUnknown>> sampleRequests_;

    uint64_t sampleIndex_{0};
    std::vector<uint8_t> frameScratch_;
};

} // namespace km::vcam
