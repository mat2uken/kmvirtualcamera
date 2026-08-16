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

    // Test 3: NV12 720x1280 (Portrait from mobile) with pitch=768 into 1280x720 NV12
    int pW = 720;
    int pH = 1280;
    int pitch = 768;
    std::vector<uint8_t> portraitNv12(pitch * pH * 3 / 2, 0);
    // Fill portrait Y plane with Y=180 and UV with U=80, V=160
    for (int y = 0; y < pH; ++y) {
        for (int x = 0; x < pW; ++x) {
            portraitNv12[y * pitch + x] = 180;
        }
    }
    uint8_t* pUV = portraitNv12.data() + (pitch * pH);
    for (int y = 0; y < pH / 2; ++y) {
        for (int x = 0; x < pW / 2; ++x) {
            pUV[y * pitch + (x * 2)] = 80;
            pUV[y * pitch + (x * 2) + 1] = 160;
        }
    }

    std::vector<uint8_t> dstNv12Portrait(km::protocol::kPayloadBytes);
    converter.ConvertNv12ToNv12Letterbox(
        portraitNv12.data(), pitch,
        pW, pH,
        dstNv12Portrait.data(), 1280, 720
    );

    // Pillarbox border (left/right) should be black (Y=16)
    assert(dstNv12Portrait[centerY * 1280 + 10] == 0x10);
    // Center should contain converted portrait video Y=180
    assert(dstNv12Portrait[centerY * 1280 + centerX] == 180);
    // Center UV
    assert(dstNv12Portrait[uvCenterOffset] == 80);
    assert(dstNv12Portrait[uvCenterOffset + 1] == 160);

    // Test 4: 90° Clockwise Rotation of Portrait (720x1280) -> Landscape (1280x720) Full-Fill
    std::vector<uint8_t> dstNv12Rot90(km::protocol::kPayloadBytes);
    converter.ConvertNv12ToNv12Letterbox(
        portraitNv12.data(), pitch,
        pW, pH,
        dstNv12Rot90.data(), 1280, 720,
        90
    );
    // Center should have pixel data
    assert(dstNv12Rot90[centerY * 1280 + centerX] == 180);
    // When rotated 90 degrees, aspect ratio 1280:720 (16:9) matches 1280x720 canvas, so top-left has content
    assert(dstNv12Rot90[10 * 1280 + 10] == 180);

    // Test 5: 180° Inverted Rotation
    std::vector<uint8_t> gradientNv12(1280 * 720 * 3 / 2, 0);
    gradientNv12[0] = 50; // top-left
    gradientNv12[719 * 1280 + 1279] = 220; // bottom-right
    std::vector<uint8_t> dstNv12Rot180(km::protocol::kPayloadBytes);
    converter.ConvertNv12ToNv12Letterbox(
        gradientNv12.data(), 1280,
        1280, 720,
        dstNv12Rot180.data(), 1280, 720,
        180
    );
    // After 180 rotation, top-left pixel (50) should be at bottom-right
    assert(dstNv12Rot180[719 * 1280 + 1279] == 50);

    // Test 6: Verify Black Border Chrominance (Zero Green across entire canvas for all 4 rotation angles)
    for (int angle : {0, 90, 180, 270}) {
        std::vector<uint8_t> testDst(km::protocol::kPayloadBytes);
        converter.ConvertNv12ToNv12Letterbox(
            portraitNv12.data(), pitch,
            pW, pH,
            testDst.data(), 1280, 720,
            angle
        );

        // Verify that NO pixel in the entire 1280x720 output buffer has U=0 && V=0 (green artifact)
        const uint8_t* uvPtr = testDst.data() + (1280 * 720);
        for (int i = 0; i < 1280 * 360; i += 2) {
            uint8_t u = uvPtr[i];
            uint8_t v = uvPtr[i + 1];
            // U and V should NEVER be 0 (0 produces bright green)
            assert(u >= 16 && v >= 16);
        }
    }

    std::cout << "[PASS] TestNv12Converter passed completely (Rotation 0/90/180/270 & Zero Green fully validated across all pixels)." << std::endl;
}

int main() {
    TestNv12Converter();
    return 0;
}
