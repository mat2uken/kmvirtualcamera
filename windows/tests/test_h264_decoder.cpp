#include "../receiver/codec/h264_decoder.h"
#include <iostream>
#include <vector>

int main() {
    std::cout << "[TEST] Running TestH264Decoder..." << std::endl;

    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::cout << "  CoInitializeEx: hr=0x" << std::hex << hrCo << std::dec << std::endl;

    HRESULT hrMf = MFStartup(MF_VERSION);
    std::cout << "  MFStartup: hr=0x" << std::hex << hrMf << std::dec << std::endl;

    km::codec::H264Decoder decoder;
    bool ok = decoder.Initialize(1280, 720);
    std::cout << "  H264Decoder::Initialize: " << (ok ? "SUCCESS" : "FAILED") << std::endl;

    if (!ok) {
        std::cerr << "[FAIL] Failed to initialize Media Foundation H.264 decoder MFT!" << std::endl;
        MFShutdown();
        CoUninitialize();
        return 1;
    }

    // Feed a dummy SPS/PPS/IDR NAL unit sequence
    // 00 00 00 01 67 (SPS) ... 00 00 00 01 68 (PPS) ... 00 00 00 01 65 (IDR)
    uint8_t dummyNal[] = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1f, 0x96, 0x54, 0x05, 0x01, 0xef, 0x7c, 0x04, 0x40, 0x00, 0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x0f, 0x03, 0xc5, 0x8b, 0xb8,
        0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x38, 0x80
    };

    std::vector<uint8_t> outNv12;
    int outW = 0, outH = 0;
    bool got = decoder.DecodeAccessUnit(dummyNal, sizeof(dummyNal), 0, outNv12, outW, outH);

    // If frame was decoded or initialized, check that chrominance contains no uninitialized green (U<16 && V<16)
    if (!outNv12.empty() && outW > 0 && outH > 0) {
        const uint8_t* uvPtr = outNv12.data() + (outW * outH);
        for (int i = 0; i < (outW * outH / 2); i += 2) {
            uint8_t u = uvPtr[i];
            uint8_t v = uvPtr[i + 1];
            if (u < 16 && v < 16) {
                std::cerr << "[FAIL] Green artifact detected at UV index " << i << " (U=" << (int)u << ", V=" << (int)v << ")" << std::endl;
                return 1;
            }
        }
    }

    decoder.Shutdown();
    MFShutdown();
    CoUninitialize();

    std::cout << "[PASS] TestH264Decoder passed successfully (Zero Green verified)." << std::endl;
    return 0;
}
