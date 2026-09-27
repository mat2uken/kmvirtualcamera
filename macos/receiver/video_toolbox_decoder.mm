#include "native_media.h"
#include "km/h264.h"
#import <Foundation/Foundation.h>
#include <VideoToolbox/VideoToolbox.h>
#include <CoreMedia/CoreMedia.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace km::mac {
struct VideoToolboxDecoder::Impl {
    // One outstanding AU. The callback only publishes into the slot; the owner
    // thread frees the slot, and with it the sample buffer, only after the slot
    // is done, so the callback can never write into released memory.
    struct Slot {
        Impl* owner = nullptr;
        uint64_t generation = 0;
        int64_t presentationUs = 0;
        CMSampleBufferRef sample = nullptr; // kept alive until the slot dies
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        OSStatus status = noErr;
        std::string error; // set when the owner must fail this slot itself
        PixelBuffer image;
        ~Slot() { if (sample) CFRelease(sample); }
    };
    VTDecompressionSessionRef session = nullptr;
    CMVideoFormatDescriptionRef format = nullptr;
    std::vector<uint8_t> sps, pps;
    bool hardware = false, needIdr = true;
    // Retired on every session teardown: a callback that belongs to an older
    // generation is dropped instead of publishing into a retired slot.
    std::atomic<uint64_t> generation{0};
    // Owner thread only. Results leave in submission order.
    std::deque<std::unique_ptr<Slot>> inflight;

    static void Output(void*, void* context, OSStatus status, VTDecodeInfoFlags,
                       CVImageBufferRef image, CMTime, CMTime) {
        if (!context) return;
        auto& slot = *static_cast<Slot*>(context);
        if (slot.generation != slot.owner->generation.load(std::memory_order_acquire)) return;
        std::lock_guard lock(slot.mutex);
        if (slot.done) return; // the owner already retired this slot
        slot.status = status;
        if (status == noErr && image) slot.image = PixelBuffer::retain(image);
        slot.done = true;
        slot.cv.notify_one();
    }
    static void Fail(Slot& slot, std::string message) {
        std::lock_guard lock(slot.mutex);
        if (slot.done) return;
        slot.error = std::move(message);
        slot.done = true;
        slot.cv.notify_one();
    }
    // Drain every callback this session can still deliver, retire its
    // generation, then invalidate it. Completed slots stay in the window so
    // results keep their order; only slots that never completed are failed.
    void close() {
        if (session) {
            VTDecompressionSessionWaitForAsynchronousFrames(session);
            ++generation; // any callback still pending now belongs to a dead session
            VTDecompressionSessionInvalidate(session);
            CFRelease(session);
            session = nullptr;
        }
        for (auto& slot : inflight) {
            if (slot) Fail(*slot, "Decoder session invalidated before the frame completed");
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
void VideoToolboxDecoder::reset() {
    impl_->close();
    impl_->sps.clear();
    impl_->pps.clear();
}
bool VideoToolboxDecoder::hardwareActive() const { return impl_->hardware; }
size_t VideoToolboxDecoder::outstanding() const { return impl_->inflight.size(); }

VideoToolboxDecoder::SubmitOutcome VideoToolboxDecoder::submit(
    std::span<const uint8_t> annexB, int64_t pts, std::string& error) {
    error.clear();
    std::vector<h264::Bytes> nalus;
    if (!h264::SplitAnnexB(annexB, nalus)) {
        error = "Malformed Annex-B access unit";
        return SubmitOutcome::Failed;
    }
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
        if (sps.empty() || pps.empty()) {
            reset();
            error = "Require a complete SPS/PPS pair";
            return SubmitOutcome::Failed;
        }
        if (sps != impl_->sps || pps != impl_->pps) {
            // Parameter set change: drain the old session first, and keep the
            // frames it already completed so results stay in submission order.
            impl_->close();
            impl_->sps = std::move(sps);
            impl_->pps = std::move(pps);
        }
    }
    if (!hasVcl) return SubmitOutcome::NoOutput; // Configuration-only AU: no output is expected.
    if (impl_->sps.empty() || impl_->pps.empty() || (impl_->needIdr && !idr)) {
        error = "Need IDR and SPS/PPS";
        return SubmitOutcome::Failed;
    }
    if (!impl_->session && !impl_->open(error)) return SubmitOutcome::Failed;
    if (impl_->inflight.size() >= kMaxOutstanding) {
        // The owner checks outstanding() before it takes work off the queue, so
        // this is only a guard against a window that grows without limit.
        error = "Decoder window is full";
        return SubmitOutcome::Failed;
    }
    std::vector<uint8_t> avcc;
    if (!h264::AnnexBToLengthPrefixed4(annexB, avcc)) {
        error = "NAL conversion failed";
        return SubmitOutcome::Failed;
    }
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
        error = "Input CMSampleBuffer: " + std::to_string(status);
        return SubmitOutcome::Failed;
    }
    auto slot = std::make_unique<Impl::Slot>();
    slot->owner = impl_.get();
    slot->generation = impl_->generation.load(std::memory_order_acquire);
    slot->presentationUs = pts;
    slot->sample = sample; // the slot owns the +1 reference until it is taken
    Impl::Slot* raw = slot.get();
    impl_->inflight.push_back(std::move(slot));
    status = VTDecompressionSessionDecodeFrame(impl_->session, sample,
        kVTDecodeFrame_EnableAsynchronousDecompression, raw, nullptr);
    if (status != noErr) {
        // No callback will come: fail the slot so the owner's take is not stuck.
        Impl::Fail(*raw, "VTDecompressionSessionDecodeFrame: " + std::to_string(status));
    }
    // The chain state moves with submission order: once an IDR has entered the
    // window the frames queued behind it are allowed to decode, even though
    // their results have not been taken yet.
    if (idr) impl_->needIdr = false;
    return SubmitOutcome::Queued;
}

void VideoToolboxDecoder::takeOldest(PixelBuffer& out, std::string& error, int64_t* presentationUs) {
    out = {};
    error.clear();
    if (presentationUs) *presentationUs = 0;
    if (impl_->inflight.empty()) {
        error = "Decoder window is empty";
        return;
    }
    std::unique_ptr<Impl::Slot> slot = std::move(impl_->inflight.front());
    impl_->inflight.pop_front();
    std::unique_lock lock(slot->mutex);
    slot->cv.wait(lock, [raw = slot.get()] { return raw->done; });
    std::string slotError = std::move(slot->error);
    const OSStatus status = slot->status;
    const int64_t pts = slot->presentationUs;
    PixelBuffer image = std::move(slot->image);
    lock.unlock();
    slot.reset(); // the callback is done, so the sample buffer can go
    if (presentationUs) *presentationUs = pts;
    if (!slotError.empty()) {
        impl_->needIdr = true;
        error = std::move(slotError);
        return;
    }
    if (status != noErr) {
        impl_->needIdr = true;
        error = "VT decode failed: " + std::to_string(status);
        return;
    }
    // needIdr is only ever raised here and cleared by a submitted IDR: a
    // later frame finishing well cannot repair a chain that broke earlier.
    out = std::move(image);
}

PixelBuffer VideoToolboxDecoder::decode(std::span<const uint8_t> annexB, int64_t pts, std::string& error) {
    if (submit(annexB, pts, error) != SubmitOutcome::Queued) return {};
    PixelBuffer image;
    takeOldest(image, error);
    return image;
}
} // namespace km::mac
