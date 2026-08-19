#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <fstream>
#include <cmath>
#include <wrl/client.h>

#include "../common/shared_memory_frame.h"
#include "../receiver/media/test_pattern_generator.h"

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << "  VCAM REAL-TIME DISPLAY CADENCE & SMOOTHNESS TEST (3.0s)   " << std::endl;
    std::cout << "============================================================" << std::endl;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return 1;
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) return 1;

    // 1. Start 30fps Shared Memory Publisher with Moving Sweep Box
    std::cout << "[1] Starting 30fps Double-Buffered Frame Publisher..." << std::endl;
    km::shm::SharedMemoryPublisher publisher;
    if (!publisher.Open()) {
        std::cerr << "Failed to open SharedMemoryPublisher" << std::endl;
        return 1;
    }

    km::media::TestPatternGenerator patternGen;
    std::atomic<bool> isRunning{true};
    std::thread framePump([&]() {
        uint64_t idx = 1;
        std::vector<uint8_t> pumpFrame(km::protocol::kPayloadBytes);
        while (isRunning) {
            int64_t tsUs = static_cast<int64_t>(GetTickCount64() * 1000);
            patternGen.GenerateFrame(pumpFrame, idx++, tsUs);
            publisher.PublishFrame(pumpFrame.data(), pumpFrame.size(), tsUs);
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
    });

    // 2. Enumerate Video Capture Devices
    Microsoft::WRL::ComPtr<IMFAttributes> attr;
    MFCreateAttributes(&attr, 1);
    attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate** ppDevices = nullptr;
    UINT32 deviceCount = 0;
    MFEnumDeviceSources(attr.Get(), &ppDevices, &deviceCount);

    IMFActivate* targetDevice = nullptr;
    for (UINT32 i = 0; i < deviceCount; ++i) {
        WCHAR friendlyName[512] = {0};
        UINT32 nameLen = 0;
        ppDevices[i]->GetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, friendlyName, 512, &nameLen);
        if (wcsstr(friendlyName, L"WebRTC Bridge") != nullptr) {
            targetDevice = ppDevices[i];
        }
    }

    if (!targetDevice) {
        std::cerr << "FAILED: WebRTC Bridge Virtual Camera not found!" << std::endl;
        isRunning = false;
        framePump.join();
        return 1;
    }

    Microsoft::WRL::ComPtr<IMFMediaSource> mediaSource;
    hr = targetDevice->ActivateObject(IID_IMFMediaSource, (void**)&mediaSource);
    if (FAILED(hr)) {
        std::cerr << "FAILED: ActivateObject hr=0x" << std::hex << hr << std::endl;
        isRunning = false;
        framePump.join();
        return 1;
    }

    Microsoft::WRL::ComPtr<IMFAttributes> readerAttr;
    MFCreateAttributes(&readerAttr, 2);
    readerAttr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

    Microsoft::WRL::ComPtr<IMFSourceReader> sourceReader;
    hr = MFCreateSourceReaderFromMediaSource(mediaSource.Get(), readerAttr.Get(), &sourceReader);
    if (FAILED(hr)) {
        std::cerr << "FAILED: MFCreateSourceReaderFromMediaSource hr=0x" << std::hex << hr << std::endl;
        isRunning = false;
        framePump.join();
        return 1;
    }

    Microsoft::WRL::ComPtr<IMFMediaType> outMediaType;
    MFCreateMediaType(&outMediaType);
    outMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    outMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    sourceReader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, outMediaType.Get());

    std::cout << "[2] Capturing 90 consecutive frames at real 30fps display rate (3.0s)..." << std::endl;
    int validFrames = 0;
    int freezeCount = 0;
    std::vector<double> interFrameIntervalsMs;
    auto lastFrameTime = std::chrono::steady_clock::now();
    auto testStartTime = std::chrono::steady_clock::now();

    LONGLONG prevTimestamp = 0;
    LONGLONG maxTsJump = 0;

    for (int attempt = 0; attempt < 120 && validFrames < 90; ++attempt) {
        DWORD streamIndex = 0, flags = 0;
        LONGLONG timestamp = 0;
        Microsoft::WRL::ComPtr<IMFSample> sample;
        hr = sourceReader->ReadSample(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            0, &streamIndex, &flags, &timestamp, &sample
        );

        if (SUCCEEDED(hr) && sample) {
            auto now = std::chrono::steady_clock::now();
            validFrames++;

            if (validFrames > 1) {
                double intervalMs = std::chrono::duration<double, std::milli>(now - lastFrameTime).count();
                interFrameIntervalsMs.push_back(intervalMs);
                if (intervalMs > 100.0) {
                    freezeCount++;
                    std::cerr << "  [WARNING] Frame " << validFrames << " freeze: " << intervalMs << "ms delay!" << std::endl;
                }

                LONGLONG tsDelta = timestamp - prevTimestamp;
                if (tsDelta > 5000000LL) { // Jump > 500ms
                    maxTsJump = (std::max)(maxTsJump, tsDelta);
                    std::cerr << "  [WARNING] Timestamp jump detected: " << (tsDelta / 10000.0) << "ms!" << std::endl;
                }
            }

            prevTimestamp = timestamp;
            lastFrameTime = now;

            if (validFrames == 1 || validFrames % 30 == 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - testStartTime).count();
                std::cout << "  -> Frame " << validFrames << "/90 received: elapsed=" << elapsed << "ms, ts=" << timestamp << std::endl;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30)); // 30fps client display render cycle
    }

    auto testEndTime = std::chrono::steady_clock::now();
    double totalElapsedSec = std::chrono::duration<double>(testEndTime - testStartTime).count();
    double measuredFps = totalElapsedSec > 0.0 ? (validFrames / totalElapsedSec) : 0.0;

    double avgInterval = 0;
    for (double v : interFrameIntervalsMs) avgInterval += v;
    if (!interFrameIntervalsMs.empty()) avgInterval /= interFrameIntervalsMs.size();

    std::cout << "\n[3] Smoothness & Frame Pacing Report..." << std::endl;
    std::cout << "  Frames Captured     : " << validFrames << " / 90 frames" << std::endl;
    std::cout << "  Total Duration      : " << totalElapsedSec << " seconds" << std::endl;
    std::cout << "  Measured FPS        : " << measuredFps << " FPS (Target: 26-32 FPS)" << std::endl;
    std::cout << "  Avg Display Interval: " << avgInterval << " ms (Target: 30-36 ms)" << std::endl;
    std::cout << "  Freeze Count (>100ms): " << freezeCount << std::endl;
    std::cout << "  Max Timestamp Jump  : " << (maxTsJump / 10000.0) << " ms" << std::endl;

    targetDevice->ShutdownObject();
    for (UINT32 i = 0; i < deviceCount; ++i) {
        ppDevices[i]->Release();
    }
    CoTaskMemFree(ppDevices);

    isRunning = false;
    framePump.join();
    publisher.Close();
    MFShutdown();
    CoUninitialize();

    if (validFrames >= 85 && freezeCount == 0 && maxTsJump == 0 && measuredFps >= 25.0 && measuredFps <= 35.0) {
        std::cout << "\n============================================================" << std::endl;
        std::cout << "  >>> SMOOTHNESS TEST PASSED: 100% FLUID, ZERO FREEZE <<<   " << std::endl;
        std::cout << "============================================================" << std::endl;
        return 0;
    } else {
        std::cerr << "\n>>> SMOOTHNESS TEST FAILED: Stutter or freeze detected! <<<" << std::endl;
        return 1;
    }
}
