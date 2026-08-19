#include "test_pattern_generator.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace km::media {

// Standard SMPTE 75% Color Bars in BT.601 YUV
struct YuvColor {
    uint8_t y;
    uint8_t u;
    uint8_t v;
};

static const YuvColor kSmpte75[8] = {
    { 180, 128, 128 }, // White 75%
    { 162, 44,  142 }, // Yellow
    { 131, 156, 44  }, // Cyan
    { 112, 72,  58  }, // Green
    { 84,  184, 198 }, // Magenta
    { 65,  100, 212 }, // Red
    { 35,  212, 114 }, // Blue
    { 16,  128, 128 }  // Black
};

TestPatternGenerator::TestPatternGenerator() {
    baseBarsY_.resize(protocol::kWidth * protocol::kHeight);
    baseBarsUV_.resize(protocol::kWidth * (protocol::kHeight / 2));

    uint32_t barWidth = protocol::kWidth / 8; // 160 px per bar
    uint32_t topHeight = 480; // Top 2/3 for color bars

    for (uint32_t y = 0; y < protocol::kHeight; ++y) {
        for (uint32_t x = 0; x < protocol::kWidth; ++x) {
            uint32_t barIdx = std::min(x / barWidth, 7u);
            if (y < topHeight) {
                baseBarsY_[y * protocol::kWidth + x] = kSmpte75[barIdx].y;
            } else {
                // Bottom 1/3: Grayscale ramp + cast blocks
                uint8_t ramp = static_cast<uint8_t>(16 + (x * (235 - 16)) / protocol::kWidth);
                baseBarsY_[y * protocol::kWidth + x] = ramp;
            }
        }
    }

    for (uint32_t uvY = 0; uvY < protocol::kHeight / 2; ++uvY) {
        for (uint32_t uvX = 0; uvX < protocol::kWidth / 2; ++uvX) {
            uint32_t srcX = uvX * 2;
            uint32_t barIdx = std::min(srcX / barWidth, 7u);
            size_t uvIdx = (uvY * (protocol::kWidth / 2) + uvX) * 2;
            if (uvY < (topHeight / 2)) {
                baseBarsUV_[uvIdx]     = kSmpte75[barIdx].u;
                baseBarsUV_[uvIdx + 1] = kSmpte75[barIdx].v;
            } else {
                // Neutral U/V for grayscale ramp
                baseBarsUV_[uvIdx]     = 128;
                baseBarsUV_[uvIdx + 1] = 128;
            }
        }
    }
}

void TestPatternGenerator::GenerateFrame(std::vector<uint8_t>& outNv12, uint64_t frameIndex, int64_t timestampUs) {
    if (outNv12.size() != protocol::kPayloadBytes) {
        outNv12.resize(protocol::kPayloadBytes);
    }

    // 1. Copy base SMPTE pattern
    std::memcpy(outNv12.data(), baseBarsY_.data(), baseBarsY_.size());
    std::memcpy(outNv12.data() + baseBarsY_.size(), baseBarsUV_.data(), baseBarsUV_.size());

    // 2. Draw a moving indicator box (sweep cursor) across the bottom strip (y: 640..700)
    uint32_t boxWidth = 80;
    uint32_t boxHeight = 40;
    uint32_t boxY = 650;
    uint32_t boxX = static_cast<uint32_t>((frameIndex * 12) % (protocol::kWidth - boxWidth));

    uint8_t* pY = outNv12.data();
    uint8_t* pUV = outNv12.data() + (protocol::kWidth * protocol::kHeight);

    // Bright cyan/yellow moving square
    for (uint32_t r = 0; r < boxHeight; ++r) {
        uint32_t curY = boxY + r;
        if (curY >= protocol::kHeight) break;
        for (uint32_t c = 0; c < boxWidth; ++c) {
            uint32_t curX = boxX + c;
            pY[curY * protocol::kWidth + curX] = 235; // Peak white
        }
    }

    for (uint32_t r = 0; r < boxHeight / 2; ++r) {
        uint32_t curUvY = (boxY / 2) + r;
        if (curUvY >= protocol::kHeight / 2) break;
        for (uint32_t c = 0; c < boxWidth / 2; ++c) {
            uint32_t curUvX = (boxX / 2) + c;
            size_t uvIdx = (curUvY * (protocol::kWidth / 2) + curUvX) * 2;
            pUV[uvIdx]     = 128; // White / neutral
            pUV[uvIdx + 1] = 128;
        }
    }
}

void TestPatternGenerator::GenerateCalibrationPattern(std::vector<uint8_t>& outNv12, uint32_t patternId) {
    if (outNv12.size() != protocol::kPayloadBytes) {
        outNv12.resize(protocol::kPayloadBytes);
    }

    uint8_t* pY = outNv12.data();
    uint8_t* pUV = outNv12.data() + (protocol::kWidth * protocol::kHeight);

    for (uint32_t y = 0; y < protocol::kHeight; ++y) {
        for (uint32_t x = 0; x < protocol::kWidth; ++x) {
            // Deterministic arithmetic pattern based on coordinates and patternId
            uint8_t yVal = static_cast<uint8_t>((x * 17 + y * 31 + patternId * 101) & 0xFF);
            pY[y * protocol::kWidth + x] = yVal;
        }
    }

    for (uint32_t uvY = 0; uvY < protocol::kHeight / 2; ++uvY) {
        for (uint32_t uvX = 0; uvX < protocol::kWidth / 2; ++uvX) {
            size_t uvIdx = (uvY * (protocol::kWidth / 2) + uvX) * 2;
            uint8_t uVal = static_cast<uint8_t>((uvX * 13 + uvY * 23 + patternId * 47) & 0xFF);
            uint8_t vVal = static_cast<uint8_t>((uvX * 29 + uvY * 7 + patternId * 83) & 0xFF);
            pUV[uvIdx]     = uVal;
            pUV[uvIdx + 1] = vVal;
        }
    }
}

} // namespace km::media
