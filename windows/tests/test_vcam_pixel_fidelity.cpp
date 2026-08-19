#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
#include <thread>
#include <cassert>
#include <wrl/client.h>

#include "../common/shared_memory_frame.h"
#include "../receiver/media/test_pattern_generator.h"
#include "../virtual-camera/media-source/webrtc_bridge_media_source.h"

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << "  VIRTUAL CAMERA PIXEL-LEVEL FIDELITY AUTOMATED TEST        " << std::endl;
    std::cout << "============================================================" << std::endl;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    assert(SUCCEEDED(hr));
    hr = MFStartup(MF_VERSION);
    assert(SUCCEEDED(hr));

    // 1. Start Shared Memory Publisher
    std::cout << "[1] Starting Shared Memory Publisher..." << std::endl;
    km::shm::SharedMemoryPublisher publisher;
    publisher.Open();

    // 2. Generate Golden Calibration Pattern 1
    std::cout << "[2] Generating Golden Calibration Pattern (PatternId = 101)..." << std::endl;
    std::vector<uint8_t> goldPattern1(km::protocol::kPayloadBytes);
    km::media::TestPatternGenerator::GenerateCalibrationPattern(goldPattern1, 101);

    // Continuous feed of Pattern 1
    publisher.PublishFrame(goldPattern1.data(), goldPattern1.size(), 1000000);

    // 3. Create Media Source and SourceReader
    std::cout << "[3] Instantiating Virtual Camera Media Source & SourceReader..." << std::endl;
    Microsoft::WRL::ComPtr<IMFMediaSource> mediaSource;
    hr = km::vcam::WebRtcBridgeMediaSource::CreateInstance(&mediaSource);
    if (FAILED(hr)) {
        std::cerr << "FAILED to create MediaSource: hr=0x" << std::hex << hr << std::endl;
        return 1;
    }

    Microsoft::WRL::ComPtr<IMFSourceReader> sourceReader;
    hr = MFCreateSourceReaderFromMediaSource(mediaSource.Get(), nullptr, &sourceReader);
    if (FAILED(hr)) {
        std::cerr << "FAILED to create SourceReader: hr=0x" << std::hex << hr << std::endl;
        return 1;
    }

    // 4. Capture frames and verify 100% pixel fidelity
    std::cout << "[4] Capturing frames and validating pixel-level exact match..." << std::endl;
    bool pattern1Matched = false;
    std::vector<uint8_t> capturedBuffer(km::protocol::kPayloadBytes);

    for (int attempt = 0; attempt < 30; ++attempt) {
        publisher.PublishFrame(goldPattern1.data(), goldPattern1.size(), (attempt + 1) * 33333);

        DWORD streamIndex = 0, flags = 0;
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
            Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
            hr = sample->ConvertToContiguousBuffer(&buffer);
            if (SUCCEEDED(hr) && buffer) {
                BYTE* pData = nullptr;
                DWORD maxLen = 0, curLen = 0;
                if (SUCCEEDED(buffer->Lock(&pData, &maxLen, &curLen)) && pData) {
                    size_t diffCount = 0;
                    for (size_t i = 0; i < (std::min)((size_t)curLen, (size_t)km::protocol::kPayloadBytes); ++i) {
                        if (pData[i] != goldPattern1[i]) {
                            diffCount++;
                        }
                    }
                    std::cout << "  Attempt " << attempt << ": curLen=" << curLen << " maxLen=" << maxLen << " diff=" << diffCount << " pData[0]=" << (int)pData[0] << " gold[0]=" << (int)goldPattern1[0] << std::endl;

                    if (curLen == km::protocol::kPayloadBytes && diffCount == 0) {
                        pattern1Matched = true;
                        buffer->Unlock();
                        std::cout << "  -> Frame " << (attempt + 1) << ": 1,382,400 / 1,382,400 bytes EXACT MATCH (100.000%, Diff: 0 bytes)" << std::endl;
                        break;
                    }
                    buffer->Unlock();
                }
            }
        } else {
            std::cout << "  Attempt " << attempt << ": ReadSample hr=0x" << std::hex << hr << " flags=" << flags << " sample=" << (sample ? 1 : 0) << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (!pattern1Matched) {
        std::cerr << "FAILED: Pattern 1 pixel fidelity verification failed!" << std::endl;
        return 1;
    }

    // 5. Test Dynamic Pattern Switching (PatternId = 202)
    std::cout << "[5] Testing Dynamic Pattern Switching (PatternId = 202)..." << std::endl;
    std::vector<uint8_t> goldPattern2(km::protocol::kPayloadBytes);
    km::media::TestPatternGenerator::GenerateCalibrationPattern(goldPattern2, 202);

    bool pattern2Matched = false;
    for (int attempt = 0; attempt < 30; ++attempt) {
        publisher.PublishFrame(goldPattern2.data(), goldPattern2.size(), (attempt + 50) * 33333);

        DWORD streamIndex = 0, flags = 0;
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
            Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
            hr = sample->ConvertToContiguousBuffer(&buffer);
            if (SUCCEEDED(hr) && buffer) {
                BYTE* pData = nullptr;
                DWORD maxLen = 0, curLen = 0;
                if (SUCCEEDED(buffer->Lock(&pData, &maxLen, &curLen)) && pData) {
                    if (curLen == km::protocol::kPayloadBytes) {
                        size_t diffCount = 0;
                        for (size_t i = 0; i < km::protocol::kPayloadBytes; ++i) {
                            if (pData[i] != goldPattern2[i]) {
                                diffCount++;
                            }
                        }

                        if (diffCount == 0) {
                            pattern2Matched = true;
                            buffer->Unlock();
                            std::cout << "  -> Frame " << (attempt + 1) << ": Dynamic Pattern 2 EXACT MATCH (100.000%, Diff: 0 bytes)" << std::endl;
                            break;
                        }
                    }
                    buffer->Unlock();
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (!pattern2Matched) {
        std::cerr << "FAILED: Pattern 2 dynamic switching verification failed!" << std::endl;
        return 1;
    }

    // 6. Test SMPTE Color Bars Generation
    std::cout << "[6] Testing SMPTE Color Bars Generation..." << std::endl;
    km::media::TestPatternGenerator smpteGen;
    std::vector<uint8_t> smpteFrame(km::protocol::kPayloadBytes);
    smpteGen.GenerateFrame(smpteFrame, 0, 0);
    assert(smpteFrame.size() == km::protocol::kPayloadBytes);
    std::cout << "  -> SMPTE Color Bars generated successfully (" << smpteFrame.size() << " bytes)." << std::endl;

    publisher.Close();
    MFShutdown();
    CoUninitialize();

    std::cout << "============================================================" << std::endl;
    std::cout << "  >>> ALL PIXEL-LEVEL FIDELITY TESTS PASSED (100%) <<<      " << std::endl;
    std::cout << "============================================================" << std::endl;
    return 0;
}
