#include "native_media.h"
#include "km/h264.h"
#import <Foundation/Foundation.h>
#include <VideoToolbox/VideoToolbox.h>
#include <CoreMedia/CoreMedia.h>
#include <mutex>
#include <vector>

namespace km::mac {
struct VideoToolboxDecoder::Impl {
    VTDecompressionSessionRef session = nullptr;
    CMVideoFormatDescriptionRef format = nullptr;
    std::vector<uint8_t> sps, pps;
    bool hardware = false, needIdr = true;
    struct Result { std::mutex mutex; PixelBuffer image; OSStatus status = noErr; };
    static void Output(void*, void* context, OSStatus status, VTDecodeInfoFlags,
                       CVImageBufferRef image, CMTime, CMTime) {
        if (!context) return;
        auto& r = *static_cast<Result*>(context);
        std::lock_guard lock(r.mutex);
        r.status = status;
        if (status == noErr && image) r.image = PixelBuffer::retain(image);
    }
    void close() {
        if (session) {
            VTDecompressionSessionWaitForAsynchronousFrames(session);
            VTDecompressionSessionInvalidate(session);
            CFRelease(session);
            session = nullptr;
        }
        if (format) { CFRelease(format); format = nullptr; }
        hardware = false;
        needIdr = true;
    }
    ~Impl() { close(); }
    bool open(std::string& error) {
        const uint8_t* sets[] = {sps.data(), pps.data()};
        size_t sizes[] = {sps.size(), pps.size()};
        OSStatus status = CMVideoFormatDescriptionCreateFromH264ParameterSets(
            kCFAllocatorDefault, 2, sets, sizes, 4, &format);
        if (status != noErr) { error = "H.264 format: " + std::to_string(status); return false; }
        const auto dims = CMVideoFormatDescriptionGetDimensions(format);
        if (dims.width <= 0 || dims.height <= 0 || dims.width > 8192 || dims.height > 8192) {
            error = "H.264 coded dimensions exceed limit"; close(); return false;
        }
        NSDictionary* spec = @{
            (__bridge NSString*)kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder: @YES
        };
        NSDictionary* attrs = @{
            (__bridge NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
            (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{},
            (__bridge NSString*)kCVPixelBufferMetalCompatibilityKey: @YES
        };
        VTDecompressionOutputCallbackRecord cb = {&Output, nullptr};
        status = VTDecompressionSessionCreate(kCFAllocatorDefault, format, (__bridge CFDictionaryRef)spec,
            (__bridge CFDictionaryRef)attrs, &cb, &session);
        if (status != noErr) { error = "VTDecompressionSessionCreate: " + std::to_string(status); close(); return false; }
        // Preference, not a guarantee. An unsupported RealTime property is non-fatal.
        VTSessionSetProperty(session, kVTDecompressionPropertyKey_RealTime, kCFBooleanTrue);
        CFTypeRef active = nullptr;
        if (VTSessionCopyProperty(session, kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder,
            kCFAllocatorDefault, &active) == noErr && active) {
            hardware = CFEqual(active, kCFBooleanTrue);
            CFRelease(active);
        }
        return true;
    }
};
VideoToolboxDecoder::VideoToolboxDecoder() : impl_(std::make_unique<Impl>()) {}
VideoToolboxDecoder::~VideoToolboxDecoder() = default;
void VideoToolboxDecoder::reset() { impl_->close(); impl_->sps.clear(); impl_->pps.clear(); }
bool VideoToolboxDecoder::hardwareActive() const { return impl_->hardware; }
PixelBuffer VideoToolboxDecoder::decode(std::span<const uint8_t> annexB, int64_t pts, std::string& error) {
    error.clear();
    std::vector<h264::Bytes> nalus;
    if (!h264::SplitAnnexB(annexB, nalus)) { error = "Malformed Annex-B access unit"; return {}; }
    std::vector<uint8_t> sps, pps;
    bool idr = false, hasVcl = false;
    for (auto n : nalus) {
        const auto type = n[0] & 31;
        if (type == 7) sps.assign(n.begin(), n.end());
        if (type == 8) pps.assign(n.begin(), n.end());
        if (type == 5) idr = true;
        if (type == 1 || type == 5) hasVcl = true;
    }
    if (!sps.empty() || !pps.empty()) {
        if (sps.empty() || pps.empty()) { reset(); error = "Require a complete SPS/PPS pair"; return {}; }
        if (sps != impl_->sps || pps != impl_->pps) {
            impl_->close(); impl_->sps = std::move(sps); impl_->pps = std::move(pps);
        }
    }
    if (!hasVcl) return {}; // Configuration-only AU: no output is expected.
    if (impl_->sps.empty() || impl_->pps.empty() || (impl_->needIdr && !idr)) {
        error = "Need IDR and SPS/PPS"; return {};
    }
    if (!impl_->session && !impl_->open(error)) return {};
    std::vector<uint8_t> avcc;
    if (!h264::AnnexBToLengthPrefixed4(annexB, avcc)) { error = "NAL conversion failed"; return {}; }
    CMBlockBufferRef block = nullptr;
    CMSampleBufferRef sample = nullptr;
    OSStatus status = CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, nullptr, avcc.size(),
        kCFAllocatorDefault, nullptr, 0, avcc.size(), 0, &block);
    if (status == noErr) status = CMBlockBufferReplaceDataBytes(avcc.data(), block, 0, avcc.size());
    const size_t size = avcc.size();
    CMSampleTimingInfo timing = {kCMTimeInvalid, CMTimeMake(pts, 1000000), kCMTimeInvalid};
    if (status == noErr) status = CMSampleBufferCreateReady(kCFAllocatorDefault, block, impl_->format,
        1, 1, &timing, 1, &size, &sample);
    if (block) CFRelease(block);
    if (status != noErr) {
        if (sample) CFRelease(sample);
        error = "Input CMSampleBuffer: " + std::to_string(status); return {};
    }
    Impl::Result result;
    status = VTDecompressionSessionDecodeFrame(impl_->session, sample,
        kVTDecodeFrame_EnableAsynchronousDecompression, &result, nullptr);
    // Callback context remains alive until asynchronous work is drained, including error paths.
    const OSStatus waitStatus = VTDecompressionSessionWaitForAsynchronousFrames(impl_->session);
    CFRelease(sample);
    if (waitStatus != noErr) {
        impl_->close(); error = "VT wait failed: " + std::to_string(waitStatus); return {};
    }
    std::lock_guard lock(result.mutex);
    if (status != noErr || result.status != noErr) {
        impl_->needIdr = true;
        error = "VT decode failed: " + std::to_string(status != noErr ? status : result.status); return {};
    }
    if (result.image) impl_->needIdr = false;
    return std::move(result.image);
}
} // namespace km::mac
