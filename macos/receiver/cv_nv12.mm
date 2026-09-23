#include "native_media.h"
#include "km/nv12.h"
#import <Foundation/Foundation.h>

namespace km::mac {
namespace {
PixelBuffer Allocate(std::string& error) {
    CVPixelBufferRef raw = nullptr;
    NSDictionary* attrs = @{
        (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{},
        (__bridge NSString*)kCVPixelBufferMetalCompatibilityKey: @YES
    };
    const CVReturn status = CVPixelBufferCreate(kCFAllocatorDefault, 1280, 720,
        kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, (__bridge CFDictionaryRef)attrs, &raw);
    if (status != kCVReturnSuccess) error = "CVPixelBufferCreate: " + std::to_string(status);
    return PixelBuffer(raw);
}
struct Lock {
    CVPixelBufferRef value;
    CVOptionFlags flags;
    CVReturn status;
    Lock(CVPixelBufferRef b, CVOptionFlags f)
        : value(b), flags(f), status(CVPixelBufferLockBaseAddress(b, f)) {}
    ~Lock() { if (status == kCVReturnSuccess) CVPixelBufferUnlockBaseAddress(value, flags); }
};
km::MutableNv12View View(CVPixelBufferRef b) {
    const auto sy = CVPixelBufferGetBytesPerRowOfPlane(b, 0);
    const auto su = CVPixelBufferGetBytesPerRowOfPlane(b, 1);
    const auto h = CVPixelBufferGetHeight(b);
    return {{static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(b, 0)), sy*h},
            {static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(b, 1)), su*(h/2)},
            sy, su, int(CVPixelBufferGetWidth(b)), int(h)};
}
}
PixelBuffer MakeBlack720p(std::string& error) {
    error.clear();
    auto out = Allocate(error);
    if (!out) return {};
    Lock lock(out.get(), 0);
    if (lock.status != kCVReturnSuccess || !km::FillBlack(View(out.get()))) {
        error = "Cannot map/fill output pixel buffer";
        return {};
    }
    CVBufferSetAttachment(out.get(), kCVImageBufferYCbCrMatrixKey,
        kCVImageBufferYCbCrMatrix_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(out.get(), kCVImageBufferColorPrimariesKey,
        kCVImageBufferColorPrimaries_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(out.get(), kCVImageBufferTransferFunctionKey,
        kCVImageBufferTransferFunction_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
    return out;
}
PixelBuffer Normalize720p(CVPixelBufferRef input, int rotation, std::string& error) {
    error.clear();
    if (!input || CVPixelBufferGetPixelFormatType(input) != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ||
        CVPixelBufferGetPlaneCount(input) != 2 ||
        (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270)) {
        error = "Expected video-range bi-planar 420v and quadrant rotation";
        return {};
    }
    if (CVPixelBufferGetWidth(input) > 8192 || CVPixelBufferGetHeight(input) > 8192) {
        error = "Unsupported frame dimensions";
        return {};
    }
    // Reject only non-identity source geometry. VideoToolbox attaches an identity
    // pixel-aspect (1:1) to decoded frames, so a bare presence check would stop video.
    if (CFTypeRef aperture = CVBufferGetAttachment(input, kCVImageBufferCleanApertureKey, nullptr)) {
        error = "Clean-aperture normalization is not implemented: " + [&] {
            CFStringRef desc = CFCopyDescription(aperture);
            char buffer[256];
            const bool ok = desc && CFStringGetCString(desc, buffer, sizeof(buffer), kCFStringEncodingUTF8);
            if (desc) CFRelease(desc);
            return ok ? std::string(buffer) : std::string("(unknown)");
        }();
        return {};
    }
    if (CFTypeRef aspect = CVBufferGetAttachment(input, kCVImageBufferPixelAspectRatioKey, nullptr)) {
        CFDictionaryRef dict = (CFDictionaryRef)aspect;
        auto number = [&](CFStringRef key) -> int {
            CFTypeRef value = CFDictionaryGetValue(dict, key);
            if (!value || CFGetTypeID(value) != CFNumberGetTypeID()) return -1;
            int out = -1;
            CFNumberGetValue((CFNumberRef)value, kCFNumberIntType, &out);
            return out;
        };
        const int horizontal = number(kCVImageBufferPixelAspectRatioHorizontalSpacingKey);
        const int vertical = number(kCVImageBufferPixelAspectRatioVerticalSpacingKey);
        if (horizontal != 1 || vertical != 1) {
            error = "Non-square pixel aspect (" + std::to_string(horizontal) + ":" +
                    std::to_string(vertical) + ") is not implemented";
            return {};
        }
    }
    if (rotation == 0 && CVPixelBufferGetWidth(input) == 1280 && CVPixelBufferGetHeight(input) == 720)
        return PixelBuffer::retain(input);
    auto out = Allocate(error);
    if (!out) return {};
    Lock srcLock(input, kCVPixelBufferLock_ReadOnly), dstLock(out.get(), 0);
    if (srcLock.status != kCVReturnSuccess || dstLock.status != kCVReturnSuccess) {
        error = "Cannot map NV12 planes";
        return {};
    }
    auto s = View(input);
    if (!km::Letterbox({s.y, s.uv, s.strideY, s.strideUV, s.width, s.height}, View(out.get()), rotation)) {
        error = "Invalid plane dimensions/strides";
        return {};
    }
    // Preserve color description, but not stale source geometry. No matrix/range conversion here.
    for (CFStringRef key : {kCVImageBufferYCbCrMatrixKey, kCVImageBufferColorPrimariesKey,
                           kCVImageBufferTransferFunctionKey}) {
        if (CFTypeRef value = CVBufferGetAttachment(input, key, nullptr))
            CVBufferSetAttachment(out.get(), key, value, kCVAttachmentMode_ShouldPropagate);
    }
    return out;
}
} // namespace km::mac
