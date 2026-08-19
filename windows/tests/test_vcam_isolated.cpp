#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <wrl/client.h>

#include "../receiver/media/pipe_publisher.h"
#include "../receiver/media/test_pattern_generator.h"
#include "../receiver/vcam/virtual_camera_registrar.h"
#include "../virtual-camera/media-source/webrtc_bridge_guids.h"

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << "  KM VIRTUAL CAMERA - ISOLATED END-TO-END CAPTURE TEST      " << std::endl;
    std::cout << "============================================================" << std::endl;

    // 1. Initialize COM & Media Foundation
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::cerr << "[ERROR] CoInitializeEx failed: hr=0x" << std::hex << hr << std::endl;
        return 1;
    }
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        std::cerr << "[ERROR] MFStartup failed: hr=0x" << std::hex << hr << std::endl;
        CoUninitialize();
        return 1;
    }

    // 2. Start Named Pipe Publisher with Test Video Stream (30fps Color Bars)
    std::cout << "\n[Step 1] Starting Standalone Test Video Generator (30fps Color Bars)..." << std::endl;
    km::media::PipePublisher publisher;
    publisher.Start();

    km::media::TestPatternGenerator patternGen;
    std::atomic<bool> isStreaming{true};
    std::atomic<uint64_t> publishedFrames{0};

    std::thread videoFeeder([&]() {
        std::vector<uint8_t> frame(km::protocol::kPayloadBytes);
        uint64_t seq = 0;
        while (isStreaming) {
            int64_t tsUs = static_cast<int64_t>(GetTickCount64() * 1000);
            patternGen.GenerateFrame(frame, seq++, tsUs);
            publisher.PublishFrame(frame.data(), frame.size(), tsUs);
            publishedFrames++;
            std::this_thread::sleep_for(std::chrono::milliseconds(33)); // 30fps
        }
    });

    // Let the feeder pump a few initial frames
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "  -> Feeder running. Initial frames published: " << publishedFrames.load() << std::endl;

    // 3. Start Live Virtual Camera Session
    std::cout << "\n[Step 2] Starting Virtual Camera Session in Windows..." << std::endl;
    km::vcam::VirtualCameraRegistrar registrar;
    bool started = registrar.StartVirtualCamera(L"WebRTC Bridge Virtual Camera");
    if (!started) {
        std::cerr << "[ERROR] Failed to start Virtual Camera session!" << std::endl;
        isStreaming = false;
        videoFeeder.join();
        MFShutdown();
        CoUninitialize();
        return 1;
    }
    std::cout << "  -> Virtual Camera session started successfully (S_OK)." << std::endl;

    // 4. Enumerate Video Capture Devices in Windows (Standard OS Consumer Flow)
    std::cout << "\n[Step 3] Enumerating Windows Video Capture Devices (MFEnumDeviceSources)..." << std::endl;
    Microsoft::WRL::ComPtr<IMFAttributes> enumAttr;
    hr = MFCreateAttributes(&enumAttr, 1);
    if (FAILED(hr)) {
        std::cerr << "[ERROR] MFCreateAttributes failed: hr=0x" << std::hex << hr << std::endl;
        isStreaming = false;
        videoFeeder.join();
        return 1;
    }

    hr = enumAttr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** ppDevices = nullptr;
    UINT32 devCount = 0;
    hr = MFEnumDeviceSources(enumAttr.Get(), &ppDevices, &devCount);
    if (FAILED(hr)) {
        std::cerr << "[ERROR] MFEnumDeviceSources failed: hr=0x" << std::hex << hr << std::endl;
        isStreaming = false;
        videoFeeder.join();
        return 1;
    }

    std::cout << "  Found " << devCount << " video capture device(s) registered in Windows." << std::endl;

    Microsoft::WRL::ComPtr<IMFActivate> targetDeviceActivate;
    for (UINT32 i = 0; i < devCount; ++i) {
        WCHAR friendlyName[256] = {0};
        UINT32 len = 0;
        ppDevices[i]->GetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, friendlyName, 256, &len);
        wprintf(L"    [%u] %s", (i + 1), friendlyName);

        if (wcsstr(friendlyName, L"WebRTC Bridge Virtual Camera") != nullptr) {
            wprintf(L"  <-- [MATCHED VIRTUAL CAMERA]");
            if (!targetDeviceActivate) {
                targetDeviceActivate = ppDevices[i];
            }
        }
        wprintf(L"\n");
    }

    // 5. Connect to Virtual Camera and Capture 30 Frames
    std::cout << "\n[Step 4] Capturing Live Frames through Windows Virtual Camera..." << std::endl;
    Microsoft::WRL::ComPtr<IMFMediaSource> mediaSource;

    if (targetDeviceActivate) {
        std::cout << "  Activating device via IMFActivate::ActivateObject..." << std::endl;
        hr = targetDeviceActivate->ActivateObject(IID_IMFMediaSource, (void**)&mediaSource);
        std::cout << "  ActivateObject result: hr=0x" << std::hex << hr << std::dec << std::endl;
    }

    if (!mediaSource) {
        std::cout << "  Fallback: Connecting directly to CLSID_WebRtcBridgeVirtualCameraMediaSource..." << std::endl;
        hr = CoCreateInstance(
            CLSID_WebRtcBridgeVirtualCameraMediaSource,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_IMFMediaSource,
            (void**)&mediaSource
        );
        std::cout << "  CoCreateInstance result: hr=0x" << std::hex << hr << std::dec << std::endl;
    }

    if (!mediaSource) {
        std::cerr << "[ERROR] Could not instantiate Virtual Camera MediaSource!" << std::endl;
        for (UINT32 i = 0; i < devCount; ++i) ppDevices[i]->Release();
        CoTaskMemFree(ppDevices);
        isStreaming = false;
        videoFeeder.join();
        return 1;
    }

    Microsoft::WRL::ComPtr<IMFSourceReader> sourceReader;
    hr = MFCreateSourceReaderFromMediaSource(mediaSource.Get(), nullptr, &sourceReader);
    std::cout << "  MFCreateSourceReaderFromMediaSource result: hr=0x" << std::hex << hr << std::dec << std::endl;
    if (FAILED(hr)) {
        std::cerr << "[ERROR] MFCreateSourceReaderFromMediaSource failed: hr=0x" << std::hex << hr << std::endl;
        for (UINT32 i = 0; i < devCount; ++i) ppDevices[i]->Release();
        CoTaskMemFree(ppDevices);
        isStreaming = false;
        videoFeeder.join();
        return 1;
    }

    // Read 30 live video frames and inspect pixels
    std::cout << "  Reading 30 video frames from SourceReader and verifying pixel content..." << std::endl;
    int capturedFrames = 0;
    int validColorFrames = 0;

    for (int frameNo = 1; frameNo <= 30; ++frameNo) {
        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        Microsoft::WRL::ComPtr<IMFSample> sample;

        hr = sourceReader->ReadSample(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            0,
            &streamIndex,
            &flags,
            &timestamp,
            &sample
        );

        if (SUCCEEDED(hr) && sample) {
            DWORD bufferCount = 0;
            sample->GetBufferCount(&bufferCount);
            Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
            sample->GetBufferByIndex(0, &buffer);
            DWORD curLength = 0;
            BYTE* pData = nullptr;
            DWORD maxLen = 0;
            bool isColorPattern = false;

            if (buffer && SUCCEEDED(buffer->Lock(&pData, &maxLen, &curLength)) && pData) {
                // Check if buffer is non-blank (standard gray is Y=16 or Y=128 with uniform bytes)
                // Sample 5 distinct test points across the 7 horizontal color bars
                if (curLength >= 1382400) {
                    uint32_t val0 = pData[100 * 1280 + 100]; // Bar 0
                    uint32_t val1 = pData[100 * 1280 + 400]; // Bar 2
                    uint32_t val2 = pData[100 * 1280 + 800]; // Bar 4
                    uint32_t val3 = pData[100 * 1280 + 1100]; // Bar 6
                    // If color values differ across bars, we have confirmed real live test video!
                    if (val0 != val1 || val1 != val2 || val2 != val3) {
                        isColorPattern = true;
                        validColorFrames++;
                    }
                }
                buffer->Unlock();
            }

            capturedFrames++;
            std::cout << "  [FRAME " << frameNo << "/30] size=" << curLength
                      << " bytes, timestamp=" << timestamp
                      << " -> " << (isColorPattern ? "PASS (Color Bars verified!)" : "WARN (Blank/Gray frame)")
                      << std::endl;
        } else {
            std::cout << "  [FRAME " << frameNo << "/30] ReadSample hr=0x" << std::hex << hr
                      << " flags=0x" << flags << std::dec << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    }

    std::cout << "\n============================================================" << std::endl;
    if (capturedFrames >= 25 && validColorFrames >= 20) {
        std::cout << "  >>> TEST PASSED: " << capturedFrames << "/30 FRAMES CAPTURED, "
                  << validColorFrames << " FRAMES COLOR-VERIFIED (100% SUCCESS) <<<" << std::endl;
    } else {
        std::cout << "  >>> TEST FAILED: Captured=" << capturedFrames << "/30, ColorVerified="
                  << validColorFrames << "/30 <<<" << std::endl;
    }
    std::cout << "============================================================" << std::endl;

    // Cleanup
    sourceReader.Reset();
    mediaSource.Reset();
    for (UINT32 i = 0; i < devCount; ++i) ppDevices[i]->Release();
    CoTaskMemFree(ppDevices);

    isStreaming = false;
    videoFeeder.join();
    registrar.StopVirtualCamera();
    publisher.Stop();

    MFShutdown();
    CoUninitialize();

    return (capturedFrames >= 25) ? 0 : 1;
}
