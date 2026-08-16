#pragma once

#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mferror.h>
#include <wrl/client.h>
#include <mutex>
#include <atomic>
#include "webrtc_bridge_media_stream.h"
#include "pipe_frame_receiver.h"

namespace km::vcam {

class WebRtcBridgeMediaSource : public IMFMediaSource, public IMFGetService {
public:
    static HRESULT CreateInstance(IMFMediaSource** ppSource);

    WebRtcBridgeMediaSource();
    virtual ~WebRtcBridgeMediaSource();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IMFMediaEventGenerator
    IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState) override;
    IFACEMETHODIMP EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent) override;
    IFACEMETHODIMP GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent) override;
    IFACEMETHODIMP QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue) override;

    // IMFMediaSource
    IFACEMETHODIMP GetCharacteristics(DWORD* pdwCharacteristics) override;
    IFACEMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor** ppPresentationDescriptor) override;
    IFACEMETHODIMP Start(IMFPresentationDescriptor* pPresentationDescriptor, const GUID* pguidTimeFormat, const PROPVARIANT* pvarStartPosition) override;
    IFACEMETHODIMP Stop() override;
    IFACEMETHODIMP Pause() override;
    IFACEMETHODIMP Shutdown() override;

    // IMFGetService
    IFACEMETHODIMP GetService(REFGUID guidService, REFIID riid, void** ppvObject) override;

private:
    HRESULT Initialize();
    HRESULT CreateStreamDescriptor(IMFStreamDescriptor** ppDescriptor);

    std::atomic<ULONG> refCount_{1};
    std::mutex lock_;

    bool isInitialized_{false};
    bool isShutdown_{false};

    Microsoft::WRL::ComPtr<IMFMediaEventQueue> eventQueue_;
    Microsoft::WRL::ComPtr<IMFPresentationDescriptor> presentationDesc_;
    Microsoft::WRL::ComPtr<WebRtcBridgeMediaStream> stream_;
    PipeFrameReceiver frameReceiver_;
};

} // namespace km::vcam
