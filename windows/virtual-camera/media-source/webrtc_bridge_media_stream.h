#pragma once

#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mferror.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdint>
#include "../../common/frame_pipe_protocol.h"
#include "../../common/shared_memory_frame.h"
#include "../../common/dxgi_shared_texture.h"
#include "../../receiver/media/test_pattern_generator.h"

#include <ks.h>
#include <ksproxy.h>
#include <ksmedia.h>

namespace km::vcam {

class WebRtcBridgeMediaSource;

class WebRtcBridgeMediaStream : public IMFMediaStream2, public IMFAttributes, public IMFGetService {
public:
    WebRtcBridgeMediaStream(WebRtcBridgeMediaSource* source, IMFStreamDescriptor* streamDesc);
    virtual ~WebRtcBridgeMediaStream();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IMFAttributes
    IFACEMETHODIMP GetItem(REFGUID guidKey, PROPVARIANT* pValue) override;
    IFACEMETHODIMP GetItemType(REFGUID guidKey, MF_ATTRIBUTE_TYPE* pType) override;
    IFACEMETHODIMP CompareItem(REFGUID guidKey, REFPROPVARIANT Value, BOOL* pbResult) override;
    IFACEMETHODIMP Compare(IMFAttributes* pTheirs, MF_ATTRIBUTES_MATCH_TYPE MatchType, BOOL* pbResult) override;
    IFACEMETHODIMP GetUINT32(REFGUID guidKey, UINT32* punValue) override;
    IFACEMETHODIMP GetUINT64(REFGUID guidKey, UINT64* punValue) override;
    IFACEMETHODIMP GetDouble(REFGUID guidKey, double* pfValue) override;
    IFACEMETHODIMP GetGUID(REFGUID guidKey, GUID* pguidValue) override;
    IFACEMETHODIMP GetStringLength(REFGUID guidKey, UINT32* pcchLength) override;
    IFACEMETHODIMP GetString(REFGUID guidKey, LPWSTR pwszValue, UINT32 cchBufSize, UINT32* pcchLength) override;
    IFACEMETHODIMP GetAllocatedString(REFGUID guidKey, LPWSTR* ppwszValue, UINT32* pcchLength) override;
    IFACEMETHODIMP GetBlobSize(REFGUID guidKey, UINT32* pcbBlobSize) override;
    IFACEMETHODIMP GetBlob(REFGUID guidKey, UINT8* pBuf, UINT32 cbBufSize, UINT32* pcbBlobSize) override;
    IFACEMETHODIMP GetAllocatedBlob(REFGUID guidKey, UINT8** ppBuf, UINT32* pcbSize) override;
    IFACEMETHODIMP GetUnknown(REFGUID guidKey, REFIID riid, LPVOID* ppv) override;
    IFACEMETHODIMP SetItem(REFGUID guidKey, REFPROPVARIANT Value) override;
    IFACEMETHODIMP DeleteItem(REFGUID guidKey) override;
    IFACEMETHODIMP DeleteAllItems() override;
    IFACEMETHODIMP SetUINT32(REFGUID guidKey, UINT32 unValue) override;
    IFACEMETHODIMP SetUINT64(REFGUID guidKey, UINT64 unValue) override;
    IFACEMETHODIMP SetDouble(REFGUID guidKey, double fValue) override;
    IFACEMETHODIMP SetGUID(REFGUID guidKey, REFGUID guidValue) override;
    IFACEMETHODIMP SetString(REFGUID guidKey, LPCWSTR wszValue) override;
    IFACEMETHODIMP SetBlob(REFGUID guidKey, const UINT8* pBuf, UINT32 cbBufSize) override;
    IFACEMETHODIMP SetUnknown(REFGUID guidKey, IUnknown* pUnknown) override;
    IFACEMETHODIMP LockStore() override;
    IFACEMETHODIMP UnlockStore() override;
    IFACEMETHODIMP GetCount(UINT32* pcItems) override;
    IFACEMETHODIMP GetItemByIndex(UINT32 unIndex, GUID* pguidKey, PROPVARIANT* pValue) override;
    IFACEMETHODIMP CopyAllItems(IMFAttributes* pDest) override;

    // IMFMediaEventGenerator
    IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState) override;
    IFACEMETHODIMP EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent) override;
    IFACEMETHODIMP GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent) override;
    IFACEMETHODIMP QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue) override;

    // IMFMediaStream
    IFACEMETHODIMP GetMediaSource(IMFMediaSource** ppMediaSource) override;
    IFACEMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** ppStreamDescriptor) override;
    IFACEMETHODIMP RequestSample(IUnknown* pToken) override;

    // IMFMediaStream2
    IFACEMETHODIMP SetStreamState(MF_STREAM_STATE value) override;
    IFACEMETHODIMP GetStreamState(MF_STREAM_STATE* value) override;

    // IMFGetService
    IFACEMETHODIMP GetService(REFGUID guidService, REFIID riid, void** ppvObject) override;

    // Internal controls from MediaSource
    HRESULT Start();
    HRESULT Stop();
    HRESULT Pause();
    HRESULT Shutdown();
    HRESULT SetMediaType(IMFMediaType* pMediaType);
    HRESULT SetSampleAllocator(IUnknown* pAllocator);
    HRESULT SetD3DManager(IUnknown* pManager);

    // Work queue callback for delivering samples
    HRESULT DeliverSamples();

private:
    HRESULT CreateVideoSample(IMFSample** ppSample);
    void DeliveryThreadProc();

    std::atomic<ULONG> refCount_{1};
    WebRtcBridgeMediaSource* source_{nullptr}; // Weak ref
    Microsoft::WRL::ComPtr<IMFAttributes> streamAttributes_;
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> eventQueue_;
    Microsoft::WRL::ComPtr<IMFStreamDescriptor> streamDesc_;
    shm::SharedMemoryConsumer shmConsumer_;
    dxgi::DxgiTextureConsumer dxgiConsumer_;
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> dxgiDeviceManager_;
    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    HANDLE hD3DDevice_{nullptr};

    std::mutex lock_;
    bool isStarted_{false};
    bool isShutdown_{false};
    MF_STREAM_STATE streamState_{MF_STREAM_STATE_STOPPED};
    std::queue<Microsoft::WRL::ComPtr<IUnknown>> sampleRequests_;

    HANDLE hWakeEvent_{nullptr};
    HANDLE hSampleRequestedEvent_{nullptr};
    std::atomic<bool> isDeliveryRunning_{false};
    std::thread deliveryThread_;

    GUID currentSubType_{MFVideoFormat_NV12};
    std::atomic<uint32_t> currentFps_{60};
    Microsoft::WRL::ComPtr<IMFVideoSampleAllocator> sampleAllocator_;
    LONGLONG startTime_{0};
    LONGLONG lastSampleTime_{0};
    uint64_t sampleIndex_{0};
    std::vector<uint8_t> frameScratch_;
    media::TestPatternGenerator fallbackPatternGen_;
};

} // namespace km::vcam
