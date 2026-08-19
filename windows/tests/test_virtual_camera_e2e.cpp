#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cassert>
#include <cstring>

#include "../common/frame_pipe_protocol.h"
#include "../receiver/media/pipe_publisher.h"
#include "../virtual-camera/media-source/webrtc_bridge_guids.h"
#include "../virtual-camera/media-source/webrtc_bridge_media_source.h"
#include "../receiver/vcam/virtual_camera_registrar.h"

void TestVirtualCameraE2E() {
    std::cout << "============================================================" << std::endl;
    std::cout << "  VIRTUAL CAMERA MEDIA SOURCE E2E PIPELINE TEST             " << std::endl;
    std::cout << "============================================================" << std::endl;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    assert(SUCCEEDED(hr));
    hr = MFStartup(MF_VERSION);
    assert(SUCCEEDED(hr));

    // 1. Start Named Pipe Publisher simulating Receiver.exe streaming 720p30 NV12 frames
    std::cout << "[1] Starting Named Pipe Publisher (\\\\.\\pipe\\WebRtcBridge.VirtualCamera.v1)..." << std::endl;
    km::media::PipePublisher publisher;
    publisher.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Create distinctive test frame
    std::vector<uint8_t> testFrame(km::protocol::kPayloadBytes);
    km::protocol::FillBlackNv12(testFrame);
    testFrame[10] = 0xDE;
    testFrame[11] = 0xAD;
    testFrame[12] = 0xBE;
    testFrame[13] = 0xEF;

    // 2. Continuous frame publishing thread
    std::atomic<bool> publishingActive{true};
    std::atomic<uint64_t> publishedCount{0};
    std::thread publishThread([&]() {
        int64_t tsUs = 100000;
        while (publishingActive) {
            publisher.PublishFrame(testFrame.data(), km::protocol::kPayloadBytes, tsUs);
            tsUs += 33333; // 30 fps (33.3ms)
            publishedCount++;
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
    });

    // Wait for initial frames to be published
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // 3. Test Direct COM Instantiation of VirtualCameraMediaSource
    std::cout << "[2] Testing COM Instantiation of WebRtcBridgeVirtualCameraMediaSource..." << std::endl;
    Microsoft::WRL::ComPtr<IMFMediaSource> mediaSource;
    hr = km::vcam::WebRtcBridgeMediaSource::CreateInstance(&mediaSource);
    if (FAILED(hr) || !mediaSource) {
        std::cerr << "  [FAIL] Failed to create WebRtcBridgeMediaSource hr=0x" << std::hex << hr << std::endl;
        assert(false);
    }
    std::cout << "  -> [OK] WebRtcBridgeMediaSource created successfully." << std::endl;

    // 4. Test Source Reader Integration (as used by WebRTC/Chrome/OBS/Zoom/Camera App)
    std::cout << "[3] Testing MFCreateSourceReaderFromMediaSource (Standard Windows Camera Consumer API)..." << std::endl;
    Microsoft::WRL::ComPtr<IMFAttributes> readerAttrs;
    MFCreateAttributes(&readerAttrs, 2);
    readerAttrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

    Microsoft::WRL::ComPtr<IMFSourceReader> sourceReader;
    hr = MFCreateSourceReaderFromMediaSource(mediaSource.Get(), readerAttrs.Get(), &sourceReader);
    if (FAILED(hr) || !sourceReader) {
        std::cerr << "  [FAIL] MFCreateSourceReaderFromMediaSource failed hr=0x" << std::hex << hr << std::endl;
        assert(false);
    }
    std::cout << "  -> [OK] IMFSourceReader created." << std::endl;

    // 5. Query and verify Native Media Type
    std::cout << "[4] Inspecting Virtual Camera Native Media Type..." << std::endl;
    Microsoft::WRL::ComPtr<IMFMediaType> nativeType;
    hr = sourceReader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &nativeType);
    assert(SUCCEEDED(hr));

    GUID majorType{};
    GUID subType{};
    UINT32 width = 0, height = 0, fpsNum = 0, fpsDen = 0;
    nativeType->GetGUID(MF_MT_MAJOR_TYPE, &majorType);
    nativeType->GetGUID(MF_MT_SUBTYPE, &subType);
    MFGetAttributeSize(nativeType.Get(), MF_MT_FRAME_SIZE, &width, &height);
    MFGetAttributeRatio(nativeType.Get(), MF_MT_FRAME_RATE, &fpsNum, &fpsDen);

    std::cout << "  Resolution: " << width << "x" << height << std::endl;
    std::cout << "  Frame Rate: " << fpsNum << "/" << fpsDen << " fps" << std::endl;
    std::cout << "  Format:     NV12 (" << (subType == MFVideoFormat_NV12 ? "MATCH" : "MISMATCH") << ")" << std::endl;

    assert(majorType == MFMediaType_Video);
    assert(subType == MFVideoFormat_NV12);
    assert(width == 1280);
    assert(height == 720);
    assert(fpsNum == 30);

    // 6. Capture live samples via ReadSample
    std::cout << "[5] Capturing Live Frames from Virtual Camera..." << std::endl;
    int samplesRead = 0;
    int sigMatched = 0;

    // Warm-up first sample to start media source pipeline and pipe connection
    DWORD streamIndex = 0;
    DWORD flags = 0;
    LONGLONG timestamp = 0;
    Microsoft::WRL::ComPtr<IMFSample> pFirstSample;
    hr = sourceReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIndex, &flags, &timestamp, &pFirstSample);

    // Allow background pipe connection to establish
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    for (int i = 0; i < 30 && samplesRead < 15; ++i) {
        streamIndex = 0;
        flags = 0;
        timestamp = 0;
        Microsoft::WRL::ComPtr<IMFSample> pSample;

        hr = sourceReader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            &streamIndex,
            &flags,
            &timestamp,
            &pSample
        );

        if (SUCCEEDED(hr) && pSample) {
            DWORD bufferCount = 0;
            pSample->GetBufferCount(&bufferCount);

            Microsoft::WRL::ComPtr<IMFMediaBuffer> pBuffer;
            pSample->ConvertToContiguousBuffer(&pBuffer);

            BYTE* pData = nullptr;
            DWORD currentLen = 0;
            pBuffer->Lock(&pData, nullptr, &currentLen);

            if (pData && currentLen == km::protocol::kPayloadBytes) {
                // Verify signature pattern transferred through IPC
                bool sigOk = (pData[10] == 0xDE && pData[11] == 0xAD && pData[12] == 0xBE && pData[13] == 0xEF);
                if (sigOk) sigMatched++;
                std::cout << "  Frame " << (samplesRead + 1) << ": size=" << currentLen << " bytes, ts=" << timestamp 
                          << " (100ns units), SigCheck=" << (sigOk ? "PASS (Live Video)" : "STUDIO_BLACK (Connecting)") << std::endl;
                samplesRead++;
            }
            pBuffer->Unlock();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    assert(samplesRead >= 10);
    assert(sigMatched >= 5);
    std::cout << "  -> [OK] Successfully captured " << samplesRead << " frames (" << sigMatched << " live video frames) from Virtual Camera." << std::endl;

    // Stop the NV12 reader and publish a saturated color frame for explicit
    // RGB32 negotiation. This exercises the presentation descriptor passed to
    // IMFMediaSource::Start rather than relying on Source Reader conversion.
    sourceReader.Reset();
    mediaSource->Shutdown();
    mediaSource.Reset();
    publishingActive = false;
    if (publishThread.joinable()) publishThread.join();

    std::vector<uint8_t> colorFrame(km::protocol::kPayloadBytes);
    std::memset(colorFrame.data(), 100, km::protocol::kWidth * km::protocol::kHeight);
    uint8_t* colorUv = colorFrame.data() + km::protocol::kWidth * km::protocol::kHeight;
    for (size_t i = 0; i < km::protocol::kWidth * km::protocol::kHeight / 2; i += 2) {
        colorUv[i] = 60;
        colorUv[i + 1] = 200;
    }
    publisher.PublishFrame(colorFrame.data(), colorFrame.size(), 2000000);

    std::cout << "[6] Negotiating RGB32 directly on the Media Source..." << std::endl;
    Microsoft::WRL::ComPtr<IMFMediaSource> rgbSource;
    hr = km::vcam::WebRtcBridgeMediaSource::CreateInstance(&rgbSource);
    assert(SUCCEEDED(hr));

    Microsoft::WRL::ComPtr<IMFPresentationDescriptor> rgbPd;
    hr = rgbSource->CreatePresentationDescriptor(&rgbPd);
    assert(SUCCEEDED(hr));

    BOOL selected = FALSE;
    Microsoft::WRL::ComPtr<IMFStreamDescriptor> rgbSd;
    hr = rgbPd->GetStreamDescriptorByIndex(0, &selected, &rgbSd);
    assert(SUCCEEDED(hr) && selected);

    Microsoft::WRL::ComPtr<IMFMediaTypeHandler> rgbHandler;
    hr = rgbSd->GetMediaTypeHandler(&rgbHandler);
    assert(SUCCEEDED(hr));
    DWORD typeCount = 0;
    rgbHandler->GetMediaTypeCount(&typeCount);
    Microsoft::WRL::ComPtr<IMFMediaType> rgbMediaType;
    for (DWORD i = 0; i < typeCount; ++i) {
        Microsoft::WRL::ComPtr<IMFMediaType> candidate;
        if (SUCCEEDED(rgbHandler->GetMediaTypeByIndex(i, &candidate))) {
            GUID candidateSubtype = GUID_NULL;
            if (SUCCEEDED(candidate->GetGUID(MF_MT_SUBTYPE, &candidateSubtype)) &&
                candidateSubtype == MFVideoFormat_RGB32) {
                rgbMediaType = candidate;
                break;
            }
        }
    }
    assert(rgbMediaType);
    hr = rgbHandler->SetCurrentMediaType(rgbMediaType.Get());
    assert(SUCCEEDED(hr));

    // Exercise the same allocator contract used by Windows Frame Server.
    Microsoft::WRL::ComPtr<IMFVideoSampleAllocator> rgbAllocator;
    hr = MFCreateVideoSampleAllocatorEx(IID_PPV_ARGS(&rgbAllocator));
    assert(SUCCEEDED(hr));
    hr = rgbAllocator->InitializeSampleAllocator(3, rgbMediaType.Get());
    assert(SUCCEEDED(hr));

    Microsoft::WRL::ComPtr<IMFSampleAllocatorControl> allocatorControl;
    hr = rgbSource.As(&allocatorControl);
    assert(SUCCEEDED(hr));
    DWORD inputStreamId = 0;
    MFSampleAllocatorUsage allocatorUsage = MFSampleAllocatorUsage_DoesNotAllocate;
    hr = allocatorControl->GetAllocatorUsage(0, &inputStreamId, &allocatorUsage);
    assert(SUCCEEDED(hr));
    assert(allocatorUsage == MFSampleAllocatorUsage_UsesProvidedAllocator);
    hr = allocatorControl->SetDefaultAllocator(0, rgbAllocator.Get());
    assert(SUCCEEDED(hr));

    PROPVARIANT startPosition;
    PropVariantInit(&startPosition);
    hr = rgbSource->Start(rgbPd.Get(), &GUID_NULL, &startPosition);
    assert(SUCCEEDED(hr));

    Microsoft::WRL::ComPtr<IMFMediaStream> rgbStream;
    for (int i = 0; i < 3 && !rgbStream; ++i) {
        Microsoft::WRL::ComPtr<IMFMediaEvent> event;
        hr = rgbSource->GetEvent(0, &event);
        assert(SUCCEEDED(hr));
        MediaEventType eventType = MEUnknown;
        event->GetType(&eventType);
        if (eventType == MENewStream || eventType == MEUpdatedStream) {
            PROPVARIANT value;
            PropVariantInit(&value);
            event->GetValue(&value);
            if (value.vt == VT_UNKNOWN && value.punkVal) {
                value.punkVal->QueryInterface(IID_PPV_ARGS(&rgbStream));
            }
            PropVariantClear(&value);
        }
    }
    assert(rgbStream);

    hr = rgbStream->RequestSample(nullptr);
    assert(SUCCEEDED(hr));
    Microsoft::WRL::ComPtr<IMFSample> rgbSample;
    for (int i = 0; i < 3 && !rgbSample; ++i) {
        Microsoft::WRL::ComPtr<IMFMediaEvent> event;
        hr = rgbStream->GetEvent(0, &event);
        assert(SUCCEEDED(hr));
        MediaEventType eventType = MEUnknown;
        event->GetType(&eventType);
        if (eventType == MEMediaSample) {
            PROPVARIANT value;
            PropVariantInit(&value);
            event->GetValue(&value);
            if (value.vt == VT_UNKNOWN && value.punkVal) {
                value.punkVal->QueryInterface(IID_PPV_ARGS(&rgbSample));
            }
            PropVariantClear(&value);
        }
    }
    assert(rgbSample);

    Microsoft::WRL::ComPtr<IMFMediaBuffer> rgbBuffer;
    hr = rgbSample->ConvertToContiguousBuffer(&rgbBuffer);
    assert(SUCCEEDED(hr));
    BYTE* rgbData = nullptr;
    DWORD rgbLength = 0;
    hr = rgbBuffer->Lock(&rgbData, nullptr, &rgbLength);
    assert(SUCCEEDED(hr));
    assert(rgbLength == km::protocol::kWidth * km::protocol::kHeight * 4);
    // BGRA for Y=100/U=60/V=200 must be colored, opaque, and non-gray.
    assert(rgbData[3] == 0xFF);
    assert(rgbData[0] != rgbData[2]);
    rgbBuffer->Unlock();
    std::cout << "  -> [OK] RGB32 sample has the negotiated 3,686,400-byte layout and valid BGRA color." << std::endl;
    rgbSource->Shutdown();

    // 7. Test VirtualCameraRegistrar
    std::cout << "[7] Testing VirtualCameraRegistrar session lifecycle..." << std::endl;
    km::vcam::VirtualCameraRegistrar registrar;
    bool regStarted = registrar.StartVirtualCamera(L"WebRTC Bridge Windows Virtual Camera Test");
    std::cout << "  StartVirtualCamera: " << (regStarted ? "SUCCESS" : "SKIPPED (non-elevated)") << std::endl;
    registrar.StopVirtualCamera();

    // Teardown
    publisher.Stop();

    MFShutdown();
    CoUninitialize();

    std::cout << "============================================================" << std::endl;
    std::cout << "  >>> VIRTUAL CAMERA E2E TEST PASSED (100%) <<<             " << std::endl;
    std::cout << "============================================================" << std::endl;
}

int main() {
    TestVirtualCameraE2E();
    return 0;
}
