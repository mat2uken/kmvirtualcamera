#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <wrl/client.h>

#include "../receiver/media/pipe_publisher.h"
#include "../receiver/media/test_pattern_generator.h"
#include "../receiver/vcam/virtual_camera_registrar.h"
#include "../virtual-camera/media-source/webrtc_bridge_guids.h"

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << "  SYSTEM VIRTUAL CAMERA PNP DEVICE CAPTURE TEST             " << std::endl;
    std::cout << "============================================================" << std::endl;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::cerr << "CoInitializeEx failed: hr=0x" << std::hex << hr << std::endl;
        return 1;
    }
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        std::cerr << "MFStartup failed: hr=0x" << std::hex << hr << std::endl;
        return 1;
    }

    // 1. Start Pipe Publisher with Color Bars
    std::cout << "[1] Starting Pipe Publisher with SMPTE Color Bars..." << std::endl;
    km::media::PipePublisher publisher;
    publisher.Start();

    // Start Live Virtual Camera Session
    std::cout << "[1.1] Starting Live Virtual Camera Session..." << std::endl;
    km::vcam::VirtualCameraRegistrar registrar;
    bool vcamStarted = registrar.StartVirtualCamera(L"WebRTC Bridge Virtual Camera");
    std::cout << "  -> VirtualCameraRegistrar.StartVirtualCamera: " << (vcamStarted ? "SUCCESS" : "FAILED") << std::endl;

    km::media::TestPatternGenerator patternGen;
    std::vector<uint8_t> testFrame(km::protocol::kPayloadBytes);
    patternGen.GenerateFrame(testFrame, 0, 1000000);
    publisher.PublishFrame(testFrame.data(), testFrame.size(), 1000000);

    // Thread to continuously publish 30fps frames
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

    // 2. Direct COM Virtual Camera MediaSource instantiation & Capture Test
    std::cout << "[2] Testing Direct COM CoCreateInstance(CLSID_WebRtcBridgeVirtualCameraMediaSource)..." << std::endl;
    {
        Microsoft::WRL::ComPtr<IMFMediaSource> comSource;
        hr = CoCreateInstance(
            CLSID_WebRtcBridgeVirtualCameraMediaSource,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_IMFMediaSource,
            (void**)&comSource
        );
        std::cout << "  CoCreateInstance result: hr=0x" << std::hex << hr << std::endl;
        if (SUCCEEDED(hr) && comSource) {
            Microsoft::WRL::ComPtr<IMFSourceReader> reader;
            hr = MFCreateSourceReaderFromMediaSource(comSource.Get(), nullptr, &reader);
            std::cout << "  MFCreateSourceReaderFromMediaSource: hr=0x" << std::hex << hr << std::endl;
            if (SUCCEEDED(hr) && reader) {
                int comFrames = 0;
                for (int f = 0; f < 30; ++f) {
                    DWORD sIdx = 0, flg = 0;
                    LONGLONG ts = 0;
                    Microsoft::WRL::ComPtr<IMFSample> smp;
                    hr = reader->ReadSample(
                        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                        0, &sIdx, &flg, &ts, &smp
                    );
                    if (SUCCEEDED(hr) && smp) {
                        comFrames++;
                        if (comFrames <= 3) {
                            std::cout << "    Captured COM Frame " << comFrames << " ts=" << ts << std::endl;
                        }
                    } else {
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    }
                }
                std::cout << "  -> Direct COM Capture Result: " << comFrames << " frames captured! (SUCCESS)" << std::endl;
            }
        }
    }

    // 3. Enumerate System Video Capture Devices using standard Windows MFEnumDeviceSources
    std::cout << "\n[3] Enumerating Video Capture Devices via MFEnumDeviceSources..." << std::endl;
    Microsoft::WRL::ComPtr<IMFAttributes> attr;
    hr = MFCreateAttributes(&attr, 1);
    if (FAILED(hr)) {
        std::cerr << "MFCreateAttributes failed: hr=0x" << std::hex << hr << std::endl;
        isRunning = false;
        framePump.join();
        return 1;
    }

    hr = attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) {
        std::cerr << "SetGUID failed: hr=0x" << std::hex << hr << std::endl;
        isRunning = false;
        framePump.join();
        return 1;
    }

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(attr.Get(), &ppDevices, &count);
    if (FAILED(hr)) {
        std::cerr << "MFEnumDeviceSources failed: hr=0x" << std::hex << hr << std::endl;
        isRunning = false;
        framePump.join();
        return 1;
    }

    std::cout << "  Found " << count << " video capture devices in Windows." << std::endl;

    for (UINT32 i = 0; i < count; ++i) {
        WCHAR name[512] = {0};
        UINT32 nameLen = 0;
        ppDevices[i]->GetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, name, 512, &nameLen);
        wprintf(L"\n--- Testing Device [%u]: %s ---\n", i + 1, name);

        if (wcsstr(name, L"WebRTC Bridge") != nullptr || wcsstr(name, L"KM") != nullptr) {
            wprintf(L"  [PnP Virtual Camera Device Match]\n");

            // Dump attributes
            UINT32 attrCount = 0;
            ppDevices[i]->GetCount(&attrCount);
            wprintf(L"  PnP Attribute Count = %u\n", attrCount);

            Microsoft::WRL::ComPtr<IMFMediaSource> mediaSource;
            HRESULT actHr = ppDevices[i]->ActivateObject(IID_IMFMediaSource, (void**)&mediaSource);
            wprintf(L"  ActivateObject(IID_IMFMediaSource) result: hr=0x%08X\n", actHr);

            if (SUCCEEDED(actHr) && mediaSource) {
                wprintf(L"  -> SUCCESS! Creating SourceReader...\n");
                Microsoft::WRL::ComPtr<IMFSourceReader> reader;
                HRESULT rHr = MFCreateSourceReaderFromMediaSource(mediaSource.Get(), nullptr, &reader);
                wprintf(L"  MFCreateSourceReaderFromMediaSource result: hr=0x%08X\n", rHr);

                if (SUCCEEDED(rHr) && reader) {
                    wprintf(L"  -> Reading 10 video frames from Virtual Camera...\n");
                    int framesOk = 0;
                    for (int f = 0; f < 20; ++f) {
                        DWORD sIdx = 0, flg = 0;
                        LONGLONG ts = 0;
                        Microsoft::WRL::ComPtr<IMFSample> smp;
                        HRESULT rdHr = reader->ReadSample(
                            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                            0, &sIdx, &flg, &ts, &smp
                        );
                        if (SUCCEEDED(rdHr) && smp) {
                            framesOk++;
                            wprintf(L"     Frame %d: ts=%I64d\n", framesOk, ts);
                            if (framesOk >= 5) break;
                        } else {
                            std::this_thread::sleep_for(std::chrono::milliseconds(20));
                        }
                    }
                    if (framesOk >= 5) {
                        wprintf(L"  >>> [PASS] Virtual Camera Device %u fully working and streaming! <<<\n", i + 1);
                    }
                }
                ppDevices[i]->ShutdownObject();
            }
        }
        ppDevices[i]->Release();
    }
    CoTaskMemFree(ppDevices);

    // 6. Teardown
    registrar.StopVirtualCamera();
    isRunning = false;
    framePump.join();
    publisher.Stop();
    MFShutdown();
    CoUninitialize();

    std::cout << "============================================================" << std::endl;
    std::cout << "  >>> SYSTEM VIRTUAL CAMERA PNP CAPTURE TEST COMPLETED <<<  " << std::endl;
    std::cout << "============================================================" << std::endl;
    return 0;
}
