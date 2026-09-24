#include "generated_video.h"
#import <Foundation/Foundation.h>
#include <CoreVideo/CoreVideo.h>
#include <cstring>

namespace km::host {
namespace {
constexpr int kWidth = 1280;
constexpr int kHeight = 720;

struct Planes {
    uint8_t* y = nullptr;
    size_t yStride = 0;
    uint8_t* uv = nullptr;
    size_t uvStride = 0;
};

void FillBackground(Planes& p, uint8_t value) {
    for (int row = 0; row < kHeight; ++row) memset(p.y + row * p.yStride, value, kWidth);
    for (int row = 0; row < kHeight / 2; ++row) {
        uint8_t* line = p.uv + row * p.uvStride;
        for (int x = 0; x < kWidth; x += 2) {
            line[x] = 128;
            line[x + 1] = 128;
        }
    }
}

// Fills a rectangle; chroma is subsampled 2x2 so x/width must be even for color fills.
void FillRect(Planes& p, int x0, int y0, int w, int h, uint8_t yValue, int uvX, int uvW,
              uint8_t u, uint8_t v) {
    if (x0 < 0 || y0 < 0 || x0 + w > kWidth || y0 + h > kHeight) return;
    for (int row = y0; row < y0 + h; ++row) memset(p.y + row * p.yStride + x0, yValue, w);
    if (uvW <= 0) return;
    for (int row = y0 / 2; row < (y0 + h + 1) / 2; ++row) {
        uint8_t* line = p.uv + row * p.uvStride;
        for (int x = uvX; x < uvX + uvW; x += 2) {
            line[x] = u;
            line[x + 1] = v;
        }
    }
}

// 7-segment frame counter (progress without any text framework).
// Segment bits: a=1 b=2 c=4 d=8 e=16 f=32 g=64.
constexpr uint8_t kDigits[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

void DrawDigit(Planes& p, int digit, int x, int y, int thickness) {
    const int w = 4 * thickness, h = 8 * thickness, half = h / 2;
    const uint8_t seg = kDigits[digit % 10];
    auto bar = [&](int bx, int by, int bw, int bh) {
        FillRect(p, bx, by, bw, bh, 235, 0, 0, 0, 0);
    };
    if (seg & 1) bar(x, y, w, thickness);                             // a
    if (seg & 2) bar(x + w - thickness, y, thickness, half);          // b
    if (seg & 4) bar(x + w - thickness, y + half, thickness, half);   // c
    if (seg & 8) bar(x, y + h - thickness, w, thickness);             // d
    if (seg & 16) bar(x, y + half, thickness, half);                  // e
    if (seg & 32) bar(x, y, thickness, half);                         // f
    if (seg & 64) bar(x, y + half - thickness / 2, w, thickness);     // g
}

void DrawCounter(Planes& p, uint64_t value) {
    char digits[6];
    for (int i = 5; i >= 0; --i) {
        digits[i] = char(value % 10);
        value /= 10;
    }
    for (int i = 0; i < 6; ++i)
        DrawDigit(p, digits[i], 40 + i * 60, 40, 10);
}
} // namespace

km::mac::PixelBuffer MakeGeneratedFrame(uint64_t frameIndex, std::string& error) {
    CVPixelBufferRef raw = nullptr;
    NSDictionary* attrs = @{
        (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{},
        (__bridge NSString*)kCVPixelBufferMetalCompatibilityKey: @YES,
    };
    const CVReturn status = CVPixelBufferCreate(kCFAllocatorDefault, kWidth, kHeight,
        kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, (__bridge CFDictionaryRef)attrs, &raw);
    if (status != kCVReturnSuccess) {
        error = "CVPixelBufferCreate: " + std::to_string(status);
        return {};
    }
    km::mac::PixelBuffer buffer(raw);
    if (CVPixelBufferLockBaseAddress(raw, 0) != kCVReturnSuccess) {
        error = "CVPixelBufferLockBaseAddress failed";
        return {};
    }
    {
        Planes planes;
        planes.y = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(raw, 0));
        planes.yStride = CVPixelBufferGetBytesPerRowOfPlane(raw, 0);
        planes.uv = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(raw, 1));
        planes.uvStride = CVPixelBufferGetBytesPerRowOfPlane(raw, 1);
        if (!planes.y || !planes.uv) {
            CVPixelBufferUnlockBaseAddress(raw, 0);
            error = "pixel buffer has no plane addresses";
            return {};
        }
        FillBackground(planes, 48);
        // Moving shape: triangle-wave x/y so any two frames differ. Exact color is
        // verified later (stage 8); stage 6 only needs a visibly moving object whose
        // counter equals the frame index the host logged.
        const int shapeW = 320, shapeH = 180;
        const int travelX = kWidth - shapeW, travelY = kHeight - shapeH;
        const int64_t phaseX = int64_t(frameIndex % uint64_t(2 * travelX));
        const int64_t phaseY = int64_t(frameIndex % uint64_t(2 * travelY));
        const int x = int(phaseX < travelX ? phaseX : 2 * travelX - phaseX);
        const int y = int(phaseY < travelY ? phaseY : 2 * travelY - phaseY);
        FillRect(planes, x & ~1, y & ~1, shapeW & ~1, shapeH & ~1, 200, x & ~1, shapeW & ~1, 110,
                 150);
        DrawCounter(planes, frameIndex);
    }
    CVPixelBufferUnlockBaseAddress(raw, 0);
    CVBufferSetAttachment(raw, kCVImageBufferYCbCrMatrixKey, kCVImageBufferYCbCrMatrix_ITU_R_709_2,
        kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(raw, kCVImageBufferColorPrimariesKey,
        kCVImageBufferColorPrimaries_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(raw, kCVImageBufferTransferFunctionKey,
        kCVImageBufferTransferFunction_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
    return buffer;
}
} // namespace km::host
