#include "nv12_converter.h"
#include <algorithm>
#include <cstring>

namespace km::media {

Nv12Converter::Nv12Converter() = default;

void Nv12Converter::ConvertI420ToNv12(
    const uint8_t* srcY, int srcStrideY,
    const uint8_t* srcU, int srcStrideU,
    const uint8_t* srcV, int srcStrideV,
    int srcWidth, int srcHeight,
    uint8_t* dstNv12,
    int dstWidth, int dstHeight
) {
    if (!srcY || !srcU || !srcV || !dstNv12 || srcWidth <= 0 || srcHeight <= 0) return;

    // Fill black first (studio swing Y=16, UV=128)
    protocol::FillBlackNv12(std::span<uint8_t>(dstNv12, protocol::kPayloadBytes));

    // Calculate aspect-fit dimensions
    float srcAspect = static_cast<float>(srcWidth) / static_cast<float>(srcHeight);
    float dstAspect = static_cast<float>(dstWidth) / static_cast<float>(dstHeight);

    int fitW = dstWidth;
    int fitH = dstHeight;
    if (srcAspect > dstAspect) {
        fitH = static_cast<int>(dstWidth / srcAspect);
    } else {
        fitW = static_cast<int>(dstHeight * srcAspect);
    }

    // Ensure dimensions and offsets are even for 4:2:0 subsampling
    fitW = (fitW / 2) * 2;
    fitH = (fitH / 2) * 2;
    if (fitW <= 0 || fitH <= 0) return;

    int offsetX = ((dstWidth - fitW) / 4) * 2;
    int offsetY = ((dstHeight - fitH) / 4) * 2;

    uint8_t* dstYPlane = dstNv12;
    uint8_t* dstUvPlane = dstNv12 + (dstWidth * dstHeight);

    // Nearest-neighbor / bilinear copy for Y plane
    for (int y = 0; y < fitH; ++y) {
        int srcYIdx = (y * srcHeight) / fitH;
        const uint8_t* srcRow = srcY + (srcYIdx * srcStrideY);
        uint8_t* dstRow = dstYPlane + ((offsetY + y) * dstWidth) + offsetX;

        for (int x = 0; x < fitW; ++x) {
            int srcXIdx = (x * srcWidth) / fitW;
            dstRow[x] = srcRow[srcXIdx];
        }
    }

    // Interleave U and V into UV plane
    int srcHalfW = (srcWidth + 1) / 2;
    int srcHalfH = (srcHeight + 1) / 2;
    int fitHalfW = fitW / 2;
    int fitHalfH = fitH / 2;
    int offsetHalfX = offsetX / 2;
    int offsetHalfY = offsetY / 2;

    for (int y = 0; y < fitHalfH; ++y) {
        int srcYIdx = (y * srcHalfH) / fitHalfH;
        const uint8_t* srcURow = srcU + (srcYIdx * srcStrideU);
        const uint8_t* srcVRow = srcV + (srcYIdx * srcStrideV);
        uint8_t* dstUvRow = dstUvPlane + ((offsetHalfY + y) * dstWidth) + (offsetHalfX * 2);

        for (int x = 0; x < fitHalfW; ++x) {
            int srcXIdx = (x * srcHalfW) / fitHalfW;
            dstUvRow[x * 2] = srcURow[srcXIdx];     // U
            dstUvRow[x * 2 + 1] = srcVRow[srcXIdx]; // V
        }
    }
}

void Nv12Converter::ConvertBgraToNv12(
    const uint8_t* srcBgra,
    int srcWidth, int srcHeight, int srcStride,
    uint8_t* dstNv12,
    int dstWidth, int dstHeight
) {
    if (!srcBgra || !dstNv12 || srcWidth <= 0 || srcHeight <= 0) return;

    // Fill black
    protocol::FillBlackNv12(std::span<uint8_t>(dstNv12, protocol::kPayloadBytes));

    float srcAspect = static_cast<float>(srcWidth) / static_cast<float>(srcHeight);
    float dstAspect = static_cast<float>(dstWidth) / static_cast<float>(dstHeight);

    int fitW = dstWidth;
    int fitH = dstHeight;
    if (srcAspect > dstAspect) {
        fitH = static_cast<int>(dstWidth / srcAspect);
    } else {
        fitW = static_cast<int>(dstHeight * srcAspect);
    }

    fitW = (fitW / 2) * 2;
    fitH = (fitH / 2) * 2;
    if (fitW <= 0 || fitH <= 0) return;

    int offsetX = ((dstWidth - fitW) / 4) * 2;
    int offsetY = ((dstHeight - fitH) / 4) * 2;

    uint8_t* dstYPlane = dstNv12;
    uint8_t* dstUvPlane = dstNv12 + (dstWidth * dstHeight);

    for (int y = 0; y < fitH; ++y) {
        int srcYIdx = (y * srcHeight) / fitH;
        const uint8_t* srcRow = srcBgra + (srcYIdx * srcStride);
        uint8_t* dstYRow = dstYPlane + ((offsetY + y) * dstWidth) + offsetX;

        for (int x = 0; x < fitW; ++x) {
            int srcXIdx = (x * srcWidth) / fitW;
            const uint8_t* pixel = srcRow + (srcXIdx * 4);
            uint8_t b = pixel[0];
            uint8_t g = pixel[1];
            uint8_t r = pixel[2];

            // Standard BT.601 RGB to Y conversion
            int yVal = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            dstYRow[x] = static_cast<uint8_t>(std::clamp(yVal, 16, 235));

            if ((y % 2 == 0) && (x % 2 == 0)) {
                int uVal = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                int vVal = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;

                int uvY = (offsetY + y) / 2;
                int uvX = (offsetX + x);
                dstUvPlane[uvY * dstWidth + uvX] = static_cast<uint8_t>(std::clamp(uVal, 16, 240));
                dstUvPlane[uvY * dstWidth + uvX + 1] = static_cast<uint8_t>(std::clamp(vVal, 16, 240));
            }
        }
    }
}

} // namespace km::media
