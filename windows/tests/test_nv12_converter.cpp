#include "../receiver/media/nv12_converter.h"
#include <cassert>
#include <iostream>
#include <vector>

void TestNv12Converter() {
    std::cout << "[TEST] Running TestNv12Converter..." << std::endl;

    km::media::Nv12Converter converter;

    // Test 1: I420 640x480 aspect-fit letterbox into 1280x720 NV12
    int srcW = 640;
    int srcH = 480;
    std::vector<uint8_t> srcY(srcW * srcH, 200);
    std::vector<uint8_t> srcU((srcW / 2) * (srcH / 2), 100);
    std::vector<uint8_t> srcV((srcW / 2) * (srcH / 2), 150);

    std::vector<uint8_t> dstNv12(km::protocol::kPayloadBytes);
    converter.ConvertI420ToNv12(
        srcY.data(), srcW,
        srcU.data(), srcW / 2,
        srcV.data(), srcW / 2,
        srcW, srcH,
        dstNv12.data(), 1280, 720
    );

    // Letterbox bars (top/left/right) should have black studio Y=16
    assert(dstNv12[0] == 0x10); // top-left corner
    // Center of image should have converted video data
    int centerY = 360;
    int centerX = 640;
    assert(dstNv12[centerY * 1280 + centerX] == 200);

    // UV center
    int uvCenterOffset = (1280 * 720) + (centerY / 2) * 1280 + (centerX / 2) * 2;
    assert(dstNv12[uvCenterOffset] == 100);     // U
    assert(dstNv12[uvCenterOffset + 1] == 150); // V

    // Test 2: BGRA 1920x1080 to 1280x720 NV12
    std::vector<uint8_t> srcBgra(1920 * 1080 * 4, 0xFF); // White
    std::vector<uint8_t> dstNv12Bgra(km::protocol::kPayloadBytes);
    converter.ConvertBgraToNv12(
        srcBgra.data(), 1920, 1080, 1920 * 4,
        dstNv12Bgra.data(), 1280, 720
    );

    // Center pixel of white should be Y=235 (studio white)
    assert(dstNv12Bgra[centerY * 1280 + centerX] == 235);

    std::cout << "[PASS] TestNv12Converter passed completely." << std::endl;
}

int main() {
    TestNv12Converter();
    return 0;
}
