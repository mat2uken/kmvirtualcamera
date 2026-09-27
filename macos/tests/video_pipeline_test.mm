// M4-a: km::IVideoPipeline (Mac) contract test. Real H.264 is encoded
// in-process with VideoToolbox, so no fixture files and no network are used.
// Covers: start validation, submit before start, stale generation, malformed
// Annex-B, NeedKeyframe recovery after a decode failure, backpressure with
// dependency-chain discard, newest-frame delivery at 720p 420v under a 90°
// transform, and stop() invalidating queued work.
#include "receiver/video_pipeline.h"
#include "receiver/native_media.h"
#include "km/h264.h"
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CVMetalTexture.h>
#include <CoreVideo/CVMetalTextureCache.h>
#include <VideoToolbox/VideoToolbox.h>
#include <Metal/Metal.h>
#include <simd/simd.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <sys/resource.h>
#include <thread>
#include <vector>

#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            std::cerr << __LINE__ << ": " #x "\n";                                                                     \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (false)

namespace {
using km::EncodedVideoFrame;
using km::OutputFormat;
using km::SubmitResult;
using km::h264::AppendAnnexB;
using km::h264::Bytes;
using km::mac::VideoPipeline;
using km::mac::VideoToolboxDecoder;

constexpr int kWidth = 1280, kHeight = 720;

template <class F> bool waitUntil(F&& ready, int timeoutMs = 10000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (ready()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return ready();
}

// Framing helpers for the encoder harness. A parse counts only when it also
// yields a coded slice: an AVCC buffer whose first length is 256..511 bytes
// starts with 00 00 01 and is otherwise accepted as Annex-B with no slice.
bool HasSlice(const std::vector<Bytes>& nalus) {
    for (Bytes nalu : nalus)
        if (const uint8_t type = uint8_t(nalu[0]) & 31; type == 1 || type == 5) return true;
    return false;
}
std::string DescribeNalus(bool ok, const std::vector<Bytes>& nalus) {
    if (!ok) return "fail";
    std::string text;
    for (Bytes nalu : nalus)
        text += std::to_string(int(uint8_t(nalu[0]) & 31)) + ":" + std::to_string(nalu.size()) + " ";
    return text.empty() ? "empty" : text;
}

// Pipeline handler: records the delivered buffer and optionally blocks the
// worker thread to make queue/backpressure ordering deterministic.
struct Sink {
    std::mutex mutex;
    std::condition_variable cv;
    int entered = 0;
    int width = 0, height = 0;
    uint32_t pixelFormat = 0;
    bool block = false;
    bool release = false;

    void operator()(CVPixelBufferRef buffer) {
        std::unique_lock lock(mutex);
        ++entered;
        width = int(CVPixelBufferGetWidth(buffer));
        height = int(CVPixelBufferGetHeight(buffer));
        pixelFormat = CVPixelBufferGetPixelFormatType(buffer);
        cv.notify_all();
        if (block) cv.wait(lock, [this] { return release; });
    }
    bool waitEntered(int count) {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(10), [&] { return entered >= count; });
    }
    void openGate() {
        std::lock_guard lock(mutex);
        release = true;
        cv.notify_all();
    }
};

struct EncodedAu {
    std::vector<uint8_t> annexB;
    bool keyframe = false;
};

// Stage-8 S2 helpers: the owner fills the decoder's bounded window exactly the
// way the pipeline worker does, only while the window has room.
bool FillWindow(km::mac::VideoToolboxDecoder& decoder, const std::vector<EncodedAu>& aus,
                size_t& next, std::vector<int64_t>& submittedPts, size_t& maxOutstanding,
                std::string& error) {
    while (next < aus.size() &&
           decoder.outstanding() < km::mac::VideoToolboxDecoder::kMaxOutstanding) {
        const int64_t pts = int64_t(next) * 33333;
        const auto outcome = decoder.submit(aus[next].annexB, pts, error);
        if (outcome != km::mac::VideoToolboxDecoder::SubmitOutcome::Queued) {
            std::vector<Bytes> nalus;
            const bool ok = km::h264::SplitAnnexB(aus[next].annexB, nalus);
            std::cerr << "fill fail au=" << next << " outcome=" << int(outcome) << " err=" << error
                      << " nalus=" << DescribeNalus(ok, nalus) << "\n";
            return false;
        }
        submittedPts.push_back(pts);
        ++next;
        if (decoder.outstanding() > maxOutstanding) maxOutstanding = decoder.outstanding();
    }
    return true;
}

// One take: the frame belongs to the AU that entered the window first, so the
// results really do leave in submission order.
bool TakeOldestInOrder(km::mac::VideoToolboxDecoder& decoder,
                       const std::vector<int64_t>& submittedPts, size_t& taken,
                       std::string& error) {
    km::mac::PixelBuffer image;
    int64_t pts = -1;
    decoder.takeOldest(image, error, &pts);
    if (!error.empty() || !image) return false;
    if (taken >= submittedPts.size() || pts != submittedPts[taken]) return false;
    ++taken;
    return true;
}

// Encodes 720p H.264 on demand: first frame forced to IDR, second frame a
// dependent P frame. The AU always starts with the session SPS/PPS parameter
// sets, matching what the receiver requires for a cold decoder.
class Encoder {
public:
    Encoder() {
        OSStatus status = VTCompressionSessionCreate(kCFAllocatorDefault, kWidth, kHeight, kCMVideoCodecType_H264,
            nullptr, nullptr, nullptr, nullptr, nullptr, &session_);
        if (status != noErr) {
            error_ = "VTCompressionSessionCreate: " + std::to_string(status);
            return;
        }
        status = VTSessionSetProperty(
            session_, kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_H264_Main_AutoLevel);
        if (status == noErr) status = VTCompressionSessionPrepareToEncodeFrames(session_);
        if (status != noErr) error_ = "VT encoder setup: " + std::to_string(status);
    }
    ~Encoder() {
        if (session_) {
            VTCompressionSessionInvalidate(session_);
            CFRelease(session_);
        }
    }
    Encoder(const Encoder&) = delete;
    Encoder& operator=(const Encoder&) = delete;
    bool ok() const { return session_ != nullptr && error_.empty(); }
    const std::string& error() const { return error_; }

    bool encode(int64_t index, bool forceKeyframe, EncodedAu& out, std::string& error) {
        error.clear();
        CVPixelBufferRef pixels = nullptr;
        const CVReturn create = CVPixelBufferCreate(kCFAllocatorDefault, kWidth, kHeight,
            kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, nullptr, &pixels);
        if (create != kCVReturnSuccess) {
            error = "CVPixelBufferCreate: " + std::to_string(create);
            return false;
        }
        CVPixelBufferLockBaseAddress(pixels, 0);
        auto* y = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pixels, 0));
        const size_t yStride = CVPixelBufferGetBytesPerRowOfPlane(pixels, 0);
        for (int row = 0; row < kHeight; ++row)
            for (int col = 0; col < kWidth; ++col) y[row * yStride + col] = uint8_t((col + index * 40) & 0xFF);
        auto* uv = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pixels, 1));
        std::memset(uv, 128, CVPixelBufferGetBytesPerRowOfPlane(pixels, 1) * (kHeight / 2));
        CVPixelBufferUnlockBaseAddress(pixels, 0);

        EncodeSink sink;
        EncodeSink* sinkPtr = &sink;
        NSDictionary* props = forceKeyframe
            ? @{(__bridge NSString*)kVTEncodeFrameOptionKey_ForceKeyFrame : @YES}
            : nil;
        OSStatus status = VTCompressionSessionEncodeFrameWithOutputHandler(session_, pixels, CMTimeMake(index, 30),
            kCMTimeInvalid, (__bridge CFDictionaryRef)props, nullptr,
            ^(OSStatus code, VTEncodeInfoFlags, CMSampleBufferRef sample) {
                std::lock_guard lock(sinkPtr->mutex);
                if (code != noErr) sinkPtr->status = code;
                else if (sample) sinkPtr->samples.push_back((CMSampleBufferRef)CFRetain(sample));
                sinkPtr->cv.notify_all();
            });
        CVPixelBufferRelease(pixels);
        if (status == noErr) status = VTCompressionSessionCompleteFrames(session_, kCMTimeInvalid);
        if (status != noErr) {
            error = "VT encode: " + std::to_string(status);
            return false;
        }
        {
            std::unique_lock lock(sink.mutex);
            if (!sink.cv.wait_for(lock, std::chrono::seconds(10),
                    [&] { return sink.status != noErr || !sink.samples.empty(); })) {
                error = "VT encode output timed out";
                return false;
            }
            if (sink.status != noErr) {
                error = "VT encode output: " + std::to_string(sink.status);
                return false;
            }
        }
        CMSampleBufferRef sample;
        {
            std::lock_guard lock(sink.mutex);
            sample = sink.samples.front();
        }
        const bool built = sampleToAu(index, sample, out, error);
        CFRelease(sample);
        return built;
    }

    // SPS/PPS prefix of the session, for building a deliberately broken AU.
    const std::vector<uint8_t>& spsPps() const { return spsPps_; }
    // NAL length-field size the format description advertises; 4 means the
    // encoder emits length-prefixed sample buffers on this OS.
    int nalLengthSize() const { return nalLengthSize_; }

private:
    struct EncodeSink {
        std::mutex mutex;
        std::condition_variable cv;
        std::vector<CMSampleBufferRef> samples;
        OSStatus status = noErr;
    };

    bool sampleToAu(int64_t frameIndex, CMSampleBufferRef sample, EncodedAu& out, std::string& error) {
        CMFormatDescriptionRef format = CMSampleBufferGetFormatDescription(sample);
        if (!format) {
            error = "encoded sample has no format description";
            return false;
        }
        std::vector<uint8_t> au;
        if (spsPps_.empty()) {
            size_t count = 0, size = 0;
            int headerLength = 0;
            const uint8_t* set = nullptr;
            OSStatus status =
                CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, 0, &set, &size, &count, &headerLength);
            if (status != noErr || !count) {
                error = "H.264 parameter sets: " + std::to_string(status);
                return false;
            }
            // AVCC length-field size of this format; 4 means the sample buffer
            // is length-prefixed, which decides the framing below.
            nalLengthSize_ = headerLength;
            for (size_t i = 0; i < count; ++i) {
                set = nullptr;
                size = 0;
                status = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, i, &set, &size, &count,
                    &headerLength);
                if (status != noErr || !set || !size) {
                    error = "H.264 parameter set " + std::to_string(i) + ": " + std::to_string(status);
                    return false;
                }
                AppendAnnexB(spsPps_, Bytes(set, size));
            }
        }
        au = spsPps_;

        CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
        if (!block) {
            error = "encoded sample has no data";
            return false;
        }
        const size_t length = CMBlockBufferGetDataLength(block);
        std::vector<uint8_t> data(length);
        const OSStatus copy = CMBlockBufferCopyDataBytes(block, 0, length, data.data());
        if (copy != kCMBlockBufferNoErr) {
            error = "CMBlockBufferCopyDataBytes: " + std::to_string(copy);
            return false;
        }
        // VT emits start-code or length-prefixed NAL units depending on OS
        // version; accept both, never guess beyond that. Annex-B is tried
        // first, but a length prefix of 256..511 bytes begins with 00 00 01
        // and is therefore read as a start code, so the Annex-B reading is
        // kept only when it actually contains a coded slice.
        // The format description states how many bytes prefix each NAL unit in
        // the sample buffer. With 4 the buffer is length-prefixed and Annex-B is
        // only a fallback, because a 256..511 length begins with 00 00 01 and
        // otherwise passes the Annex-B scan with no coded slice at all.
        std::vector<Bytes> annexB, lengthPrefixed;
        const bool annexBOk = km::h264::SplitAnnexB(data, annexB);
        const bool lengthPrefixedOk = km::h264::SplitLengthPrefixed4(data, lengthPrefixed);
        const bool annexBHasSlice = annexBOk && HasSlice(annexB);
        const bool lengthPrefixedHasSlice = lengthPrefixedOk && HasSlice(lengthPrefixed);
        const bool preferLengthPrefixed = nalLengthSize_ == 4;
        const std::vector<Bytes>* primary = preferLengthPrefixed ? &lengthPrefixed : &annexB;
        const std::vector<Bytes>* secondary = preferLengthPrefixed ? &annexB : &lengthPrefixed;
        const bool primaryOk = preferLengthPrefixed ? lengthPrefixedHasSlice : annexBHasSlice;
        const bool secondaryOk = preferLengthPrefixed ? annexBHasSlice : lengthPrefixedHasSlice;
        const std::vector<Bytes>* chosen = primaryOk ? primary : (secondaryOk ? secondary : nullptr);
        if (chosen != primary) {
            // The reading the format description expects was not the one that
            // carried the slice; keep both readings in the record.
            std::cout << "sampleDiag frame=" << frameIndex << " lengthSize=" << nalLengthSize_
                      << " bytes=" << data.size() << " annexB=" << DescribeNalus(annexBOk, annexB)
                      << " lengthPrefixed=" << DescribeNalus(lengthPrefixedOk, lengthPrefixed)
                      << " chosen=" << (chosen ? (chosen == &annexB ? "annexB" : "lengthPrefixed") : "none")
                      << " hex=";
            for (size_t i = 0; i < data.size() && i < 48; ++i) {
                const char* digits = "0123456789abcdef";
                std::cout << digits[data[i] >> 4] << digits[data[i] & 15];
            }
            std::cout << "\n";
        }
        if (!chosen) {
            error = "unrecognized encoder NAL layout";
            return false;
        }
        for (Bytes nalu : *chosen) AppendAnnexB(au, nalu);
        out.annexB = std::move(au);
        out.keyframe = true;
        if (CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
            attachments && CFArrayGetCount(attachments)) {
            auto* dict = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0));
            if (CFBooleanRef notSync = static_cast<CFBooleanRef>(
                    CFDictionaryGetValue(dict, kCMSampleAttachmentKey_NotSync));
                notSync && CFBooleanGetValue(notSync))
                out.keyframe = false;
        }
        return true;
    }

    VTCompressionSessionRef session_ = nullptr;
    std::vector<uint8_t> spsPps_;
    int nalLengthSize_ = 0;
    std::string error_;
};

EncodedVideoFrame frameFrom(const EncodedAu& au, bool randomAccess, uint64_t generation, int64_t tick) {
    EncodedVideoFrame frame;
    frame.annexB = au.annexB;
    frame.mediaTicks = tick * 3000; // RTP 90 kHz at 30 fps
    frame.timestampDomain = km::TimestampDomain::Rtp90kHz;
    frame.receivedMonotonicNs = uint64_t(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    frame.randomAccess = randomAccess;
    frame.generation = generation;
    return frame;
}

//   km_macos_pipeline_test --decodeprobe [frames]
//     decodes pre-encoded AUs straight through VideoToolboxDecoder, one at a
//     time, and prints every frame that produced neither an image nor an error.
int RunDecodeProbe(int argc, char** argv) {
    int frames = 300;
    if (argc > 2) frames = std::atoi(argv[2]);
    if (frames <= 0) {
        std::cerr << "decodeprobe arguments out of range\n";
        return 1;
    }
    constexpr int kGop = 60;
    Encoder encoder;
    if (!encoder.ok()) {
        std::cerr << encoder.error() << "\n";
        return 1;
    }
    std::vector<EncodedAu> aus;
    aus.resize(size_t(frames));
    for (int i = 0; i < frames; ++i) {
        std::string encodeError;
        if (!encoder.encode(i, i % kGop == 0, aus[size_t(i)], encodeError)) {
            std::cerr << "encode frame " << i << ": " << encodeError << "\n";
            return 1;
        }
    }
    int images = 0, failed = 0, silent = 0;
    km::mac::VideoToolboxDecoder decoder;
    for (int i = 0; i < frames; ++i) {
        std::string error;
        km::mac::PixelBuffer image =
            decoder.decode(aus[size_t(i)].annexB, int64_t(i) * 33333, error);
        if (image) {
            ++images;
        } else if (!error.empty()) {
            ++failed;
            std::cout << "frame " << i << " error: " << error << "\n";
        } else {
            ++silent;
            std::cout << "frame " << i << " silent (no image, no error)\n";
        }
    }
    std::cout << "decodeprobe frames=" << frames << " images=" << images << " failed=" << failed
              << " silent=" << silent << " nalLengthSize=" << encoder.nalLengthSize() << "\n";
    return failed == 0 && silent == 0 ? 0 : 1;
}

// Stage-8 S3: Normalize720p candidate comparison. The identity path hands the
// input buffer straight back, the transform path writes one new buffer (full
// black fill, then the fitted rectangle), so each frame costs 0 or 1 copies.
// The candidates differ only in where that output buffer comes from: per-call
// CVPixelBufferCreate (baseline) or a CVPixelBufferPool. The shared
// km::Letterbox transform is identical in both, so the difference on the line
// is allocation only.
//
//   km_macos_pipeline_test --normbench [frames]
//     frames  iterations per mode (default 600)
double RusageUs() {
    struct rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return -1;
    return double(usage.ru_utime.tv_sec) * 1e6 + double(usage.ru_utime.tv_usec) +
           double(usage.ru_stime.tv_sec) * 1e6 + double(usage.ru_stime.tv_usec);
}

CVPixelBufferPoolRef MakeNormPool(int minCount, std::string& error) {
    NSDictionary* poolAttrs = @{
        (__bridge NSString*)kCVPixelBufferPoolMinimumBufferCountKey: @(minCount)
    };
    NSDictionary* pixelAttrs = @{
        (__bridge NSString*)kCVPixelBufferWidthKey: @1280,
        (__bridge NSString*)kCVPixelBufferHeightKey: @720,
        (__bridge NSString*)kCVPixelBufferPixelFormatTypeKey:
            @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
        (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{},
        (__bridge NSString*)kCVPixelBufferMetalCompatibilityKey: @YES
    };
    CVPixelBufferPoolRef pool = nullptr;
    const CVReturn status = CVPixelBufferPoolCreate(kCFAllocatorDefault,
        (__bridge CFDictionaryRef)poolAttrs, (__bridge CFDictionaryRef)pixelAttrs, &pool);
    if (status != kCVReturnSuccess || !pool)
        error = "CVPixelBufferPoolCreate: " + std::to_string(status);
    return pool;
}

// A filled input, so the timed loop reads the same memory a decoded frame
// would. IOSurface strides may exceed width; both paths must honour stride.
km::mac::PixelBuffer MakeNormInput(int width, int height, OSType format, std::string& error) {
    error.clear();
    NSDictionary* attrs = @{
        (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{},
        (__bridge NSString*)kCVPixelBufferMetalCompatibilityKey: @YES,
        // Wider than the width for both bench inputs, so the transform proves
        // it walks rows by stride instead of assuming a packed plane.
        (__bridge NSString*)kCVPixelBufferBytesPerRowAlignmentKey: @512
    };
    CVPixelBufferRef raw = nullptr;
    const CVReturn status = CVPixelBufferCreate(kCFAllocatorDefault, width, height, format,
        (__bridge CFDictionaryRef)attrs, &raw);
    if (status != kCVReturnSuccess || !raw) {
        error = "input CVPixelBufferCreate: " + std::to_string(status);
        return {};
    }
    km::mac::PixelBuffer buffer(raw);
    if (CVPixelBufferLockBaseAddress(raw, 0) != kCVReturnSuccess) {
        error = "input lock failed";
        return {};
    }
    const size_t planeCount = CVPixelBufferGetPlaneCount(raw);
    for (size_t p = 0; p < planeCount; ++p) {
        uint8_t* base = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(raw, p));
        const size_t stride = CVPixelBufferGetBytesPerRowOfPlane(raw, p);
        const size_t rows = CVPixelBufferGetHeightOfPlane(raw, p);
        const size_t cols = CVPixelBufferGetWidthOfPlane(raw, p);
        for (size_t y = 0; y < rows; ++y)
            for (size_t x = 0; x < cols; ++x)
                base[y * stride + x] = p == 0 ? uint8_t((x * 4 + y * 2) & 0xFF) : uint8_t(128 + (x & 0x3F));
    }
    CVPixelBufferUnlockBaseAddress(raw, 0);
    return buffer;
}

struct NormSample {
    double wallAvgUs = 0;
    double wallMaxUs = 0;
    double cpuUs = 0;
    bool reusesInput = false;
    int distinctOutputs = 0;
};

bool MeasureNormalize(CVPixelBufferRef input, int rotation, int frames, bool usePool,
                      CVPixelBufferPoolRef pool, NormSample& out, std::string& error) {
    std::vector<CVPixelBufferRef> seen;
    double wallSum = 0, wallMax = 0;
    const double cpu0 = RusageUs();
    for (int i = 0; i < frames; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        km::mac::PixelBuffer result =
            km::mac::Normalize720p(input, rotation, error, usePool ? pool : nullptr);
        const auto t1 = std::chrono::steady_clock::now();
        if (!result) return false;
        const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        wallSum += us;
        if (us > wallMax) wallMax = us;
        if (result.get() == input) out.reusesInput = true;
        if (std::find(seen.begin(), seen.end(), result.get()) == seen.end())
            seen.push_back(result.get());
    }
    const double cpu1 = RusageUs();
    out.wallAvgUs = wallSum / double(frames);
    out.wallMaxUs = wallMax;
    out.cpuUs = cpu1 >= 0 && cpu0 >= 0 ? (cpu1 - cpu0) / double(frames) : -1;
    out.distinctOutputs = int(seen.size());
    return true;
}

void PrintNormSample(const char* label, const NormSample& s) {
    std::cout << "  " << label << " wallAvgUs=" << s.wallAvgUs << " wallMaxUs=" << s.wallMaxUs
              << " cpuUs=" << s.cpuUs << " copies=" << (s.reusesInput ? 0 : 1)
              << " distinctOutputs=" << s.distinctOutputs << "\n";
}

// Stage-8 S3 candidate 2: the same nearest-neighbour letterbox on the GPU. The
// shader mirrors km::Letterbox arithmetic one line at a time, so the result is
// diffed against the CPU path instead of trusted. One dispatch per plane; the
// fitted rectangle and the black margin are written by the same kernel, which
// is what FillBlack plus the CPU loop do.
const char* kNormMetalSource = R"metal(
#include <metal_stdlib>
using namespace metal;
struct Params {
    int4 rect;    // ox, oy, w, h of the fitted rectangle, in plane pixels
    int4 fit;     // w, h, rotatedW, rotatedH, in plane pixels
    int4 src;     // sw, sh, rotation, plane (0 = Y, 1 = UV)
    int2 outSize; // destination plane size
    int2 pad;
};
kernel void kmLetterbox(texture2d<float, access::read> src [[texture(0)]],
                        texture2d<float, access::write> dst [[texture(1)]],
                        constant Params& p [[buffer(0)]],
                        uint2 gid [[thread_position_in_grid]]) {
    if (int(gid.x) >= p.outSize.x || int(gid.y) >= p.outSize.y) return;
    const bool inside = int(gid.x) >= p.rect.x && int(gid.x) < p.rect.x + p.fit.x &&
                        int(gid.y) >= p.rect.y && int(gid.y) < p.rect.y + p.fit.y;
    if (!inside) {
        const float black = p.src.w == 0 ? 16.0f / 255.0f : 128.0f / 255.0f;
        dst.write(float4(black, black, black, 1.0f), gid);
        return;
    }
    const int x = int(gid.x) - p.rect.x;
    const int y = int(gid.y) - p.rect.y;
    const int rx = x * p.fit.z / p.fit.x;
    const int ry = y * p.fit.w / p.fit.y;
    int sx = rx, sy = ry;
    if (p.src.z == 90) { sx = ry; sy = p.src.y - 1 - rx; }
    else if (p.src.z == 180) { sx = p.src.x - 1 - rx; sy = p.src.y - 1 - ry; }
    else if (p.src.z == 270) { sx = p.src.x - 1 - ry; sy = rx; }
    dst.write(src.read(uint2(sx, sy)), gid);
}
)metal";

struct MetalNormalize {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLComputePipelineState> pipeline = nil;
    CVMetalTextureCacheRef cache = nullptr;
};

bool MakeMetalNormalize(MetalNormalize& metal, std::string& error) {
    metal.device = MTLCreateSystemDefaultDevice();
    if (!metal.device) {
        error = "MTLCreateSystemDefaultDevice failed";
        return false;
    }
    NSError* nsError = nil;
    id<MTLLibrary> library = [metal.device
        newLibraryWithSource:[NSString stringWithUTF8String:kNormMetalSource]
                     options:nil
                       error:&nsError];
    if (!library) {
        error = std::string("Metal library: ") + nsError.localizedDescription.UTF8String;
        return false;
    }
    id<MTLFunction> function = [library newFunctionWithName:@"kmLetterbox"];
    if (!function) {
        error = "Metal function kmLetterbox not found";
        return false;
    }
    metal.pipeline = [metal.device newComputePipelineStateWithFunction:function error:&nsError];
    if (!metal.pipeline) {
        error = std::string("Metal pipeline: ") + nsError.localizedDescription.UTF8String;
        return false;
    }
    metal.queue = [metal.device newCommandQueue];
    if (!metal.queue) {
        error = "newCommandQueue failed";
        return false;
    }
    NSDictionary* usage = @{
        (__bridge NSString*)kCVMetalTextureUsage:
            @(MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite)
    };
    const CVReturn status = CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr,
        metal.device, (__bridge CFDictionaryRef)usage, &metal.cache);
    if (status != kCVReturnSuccess || !metal.cache) {
        error = "CVMetalTextureCacheCreate: " + std::to_string(status);
        return false;
    }
    return true;
}

struct NormParams {
    simd_int4 rect;
    simd_int4 fit;
    simd_int4 src;
    simd_int2 outSize;
    simd_int2 pad;
};

// Fitted rectangle and mapping constants, copied from km::Letterbox so the two
// implementations are compared rather than assumed equal.
bool FillNormParams(int rotation, int inW, int inH, int outW, int outH, NormParams& params) {
    const int unit = params.src.w ? 2 : 1;
    const int rw = (rotation % 180) ? inH : inW;
    const int rh = (rotation % 180) ? inW : inH;
    int fw = outW, fh = outH;
    if (int64_t(rw) * outH > int64_t(rh) * outW)
        fh = int(int64_t(outW) * rh / rw) & ~1;
    else
        fw = int(int64_t(outH) * rw / rh) & ~1;
    if (fw < 2 || fh < 2) return false;
    const int ox = ((outW - fw) / 2) & ~1, oy = ((outH - fh) / 2) & ~1;
    const int sw = inW / unit, sh = inH / unit;
    const int w = fw / unit, h = fh / unit;
    params.rect = simd_make_int4(ox / unit, oy / unit, w, h);
    params.fit = simd_make_int4(w, h, rw / unit, rh / unit);
    params.src = simd_make_int4(sw, sh, rotation, params.src.w);
    params.outSize = simd_make_int2(outW / unit, outH / unit);
    params.pad = simd_make_int2(0, 0);
    return true;
}

CVMetalTextureRef MakeMetalTexture(const MetalNormalize& metal, CVPixelBufferRef buffer,
                                   size_t plane, MTLPixelFormat format, std::string& error) {
    const size_t width = plane ? CVPixelBufferGetWidthOfPlane(buffer, plane)
                               : CVPixelBufferGetWidth(buffer);
    const size_t height = plane ? CVPixelBufferGetHeightOfPlane(buffer, plane)
                                : CVPixelBufferGetHeight(buffer);
    CVMetalTextureRef texture = nullptr;
    const CVReturn status = CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault, metal.cache, buffer, nullptr,
        format, width, height, uint32_t(plane), &texture);
    if (status != kCVReturnSuccess || !texture)
        error = "CVMetalTextureCacheCreateTextureFromImage: " + std::to_string(status);
    return texture;
}

// One transform: texture wrappers, one encoder with two dispatches, then wait.
// GPU time comes from the command buffer itself.
bool MetalNormalizeOnce(const MetalNormalize& metal, CVPixelBufferRef input,
                        CVPixelBufferRef output, int rotation, double& gpuUs,
                        std::string& error) {
    CVMetalTextureRef yIn = MakeMetalTexture(metal, input, 0, MTLPixelFormatR8Unorm, error);
    if (!yIn) return false;
    CVMetalTextureRef uvIn = MakeMetalTexture(metal, input, 1, MTLPixelFormatRG8Unorm, error);
    if (!uvIn) { CFRelease(yIn); return false; }
    CVMetalTextureRef yOut = MakeMetalTexture(metal, output, 0, MTLPixelFormatR8Unorm, error);
    if (!yOut) { CFRelease(uvIn); CFRelease(yIn); return false; }
    CVMetalTextureRef uvOut = MakeMetalTexture(metal, output, 1, MTLPixelFormatRG8Unorm, error);
    if (!uvOut) { CFRelease(yOut); CFRelease(uvIn); CFRelease(yIn); return false; }
    id<MTLTexture> inY = CVMetalTextureGetTexture(yIn);
    id<MTLTexture> inUV = CVMetalTextureGetTexture(uvIn);
    id<MTLTexture> outY = CVMetalTextureGetTexture(yOut);
    id<MTLTexture> outUV = CVMetalTextureGetTexture(uvOut);
    bool ok = inY && inUV && outY && outUV;
    if (ok) {
        id<MTLCommandBuffer> command = [metal.queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        [encoder setComputePipelineState:metal.pipeline];
        const int inW = int(CVPixelBufferGetWidth(input));
        const int inH = int(CVPixelBufferGetHeight(input));
        const int outW = int(CVPixelBufferGetWidth(output));
        const int outH = int(CVPixelBufferGetHeight(output));
        for (int plane = 0; plane < 2; ++plane) {
            NormParams params;
            params.src = simd_make_int4(0, 0, 0, plane);
            if (!FillNormParams(rotation, inW, inH, outW, outH, params)) {
                error = "fitted rectangle is too small";
                ok = false;
                break;
            }
            [encoder setTexture:(plane ? inUV : inY) atIndex:0];
            [encoder setTexture:(plane ? outUV : outY) atIndex:1];
            [encoder setBytes:&params length:sizeof(params) atIndex:0];
            [encoder dispatchThreads:MTLSizeMake(size_t(params.outSize.x), size_t(params.outSize.y), 1)
                threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
        }
        [encoder endEncoding];
        if (ok) {
            [command commit];
            [command waitUntilCompleted];
            gpuUs = command.GPUEndTime > command.GPUStartTime
                ? (command.GPUEndTime - command.GPUStartTime) * 1e6
                : -1;
            if (command.status == MTLCommandBufferStatusError) {
                error = std::string("Metal command buffer: ")
                    + command.error.localizedDescription.UTF8String;
                ok = false;
            }
        }
    } else {
        error = "Metal texture is nil for a plane";
    }
    CFRelease(uvOut);
    CFRelease(yOut);
    CFRelease(uvIn);
    CFRelease(yIn);
    return ok;
}

int64_t DiffByteCount(CVPixelBufferRef a, CVPixelBufferRef b) {
    if (CVPixelBufferGetWidth(a) != CVPixelBufferGetWidth(b) ||
        CVPixelBufferGetHeight(a) != CVPixelBufferGetHeight(b) ||
        CVPixelBufferGetPixelFormatType(a) != CVPixelBufferGetPixelFormatType(b)) return -1;
    CVPixelBufferLockBaseAddress(a, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferLockBaseAddress(b, kCVPixelBufferLock_ReadOnly);
    int64_t diff = 0;
    const size_t planeCount = CVPixelBufferGetPlaneCount(a);
    for (size_t p = 0; p < planeCount; ++p) {
        const uint8_t* pa = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(a, p));
        const uint8_t* pb = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(b, p));
        const size_t sa = CVPixelBufferGetBytesPerRowOfPlane(a, p);
        const size_t sb = CVPixelBufferGetBytesPerRowOfPlane(b, p);
        const size_t rows = CVPixelBufferGetHeightOfPlane(a, p);
        const size_t cols = CVPixelBufferGetWidthOfPlane(a, p);
        if (!pa || !pb) { diff = -1; break; }
        for (size_t y = 0; y < rows; ++y)
            for (size_t x = 0; x < cols; ++x)
                if (pa[y * sa + x] != pb[y * sb + x]) ++diff;
    }
    CVPixelBufferUnlockBaseAddress(b, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferUnlockBaseAddress(a, kCVPixelBufferLock_ReadOnly);
    return diff;
}

struct MetalSample {
    double wallAvgUs = 0;
    double wallMaxUs = 0;
    double gpuAvgUs = 0;
    double gpuMaxUs = 0;
    double cpuUs = 0;
    int64_t diffBytes = -1;
};

bool MeasureMetal(const MetalNormalize& metal, CVPixelBufferRef input, int rotation, int frames,
                  MetalSample& out, std::string& error) {
    double wallSum = 0, wallMax = 0, gpuSum = 0, gpuMax = 0;
    int gpuSamples = 0;
    // Same warm-up as the CPU rows, so the first dispatch does not land in max.
    {
        km::mac::PixelBuffer warm = km::mac::Normalize720p(input, rotation, error);
        if (!warm) return false;
        for (int i = 0; i < 5; ++i) {
            double warmGpuUs = -1;
            if (!MetalNormalizeOnce(metal, input, warm.get(), rotation, warmGpuUs, error))
                return false;
        }
    }
    const double cpu0 = RusageUs();
    for (int i = 0; i < frames; ++i) {
        // The pool case already showed allocation apart from the transform, so
        // the GPU row uses the same per-call create as the CPU baseline.
        CVPixelBufferRef raw = nullptr;
        NSDictionary* attrs = @{
            (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{},
            (__bridge NSString*)kCVPixelBufferMetalCompatibilityKey: @YES
        };
        const CVReturn status = CVPixelBufferCreate(kCFAllocatorDefault, 1280, 720,
            kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, (__bridge CFDictionaryRef)attrs, &raw);
        if (status != kCVReturnSuccess) {
            error = "metal output CVPixelBufferCreate: " + std::to_string(status);
            return false;
        }
        km::mac::PixelBuffer outputBuffer(raw);
        const auto t0 = std::chrono::steady_clock::now();
        double gpuUs = -1;
        const bool ok = MetalNormalizeOnce(metal, input, outputBuffer.get(), rotation, gpuUs, error);
        const auto t1 = std::chrono::steady_clock::now();
        if (!ok) return false;
        const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        wallSum += us;
        if (us > wallMax) wallMax = us;
        if (gpuUs >= 0) {
            gpuSum += gpuUs;
            if (gpuUs > gpuMax) gpuMax = gpuUs;
            ++gpuSamples;
        }
    }
    const double cpu1 = RusageUs();
    out.wallAvgUs = wallSum / double(frames);
    out.wallMaxUs = wallMax;
    out.gpuAvgUs = gpuSamples ? gpuSum / double(gpuSamples) : -1;
    out.gpuMaxUs = gpuSamples ? gpuMax : -1;
    out.cpuUs = cpu1 >= 0 && cpu0 >= 0 ? (cpu1 - cpu0) / double(frames) : -1;
    return true;
}

void PrintMetalSample(const char* label, const MetalSample& s) {
    std::cout << "  " << label << " wallAvgUs=" << s.wallAvgUs << " wallMaxUs=" << s.wallMaxUs
              << " gpuAvgUs=" << s.gpuAvgUs << " gpuMaxUs=" << s.gpuMaxUs
              << " cpuUs=" << s.cpuUs << " copies=1 diffBytes=" << s.diffBytes << "\n";
}

int RunNormBench(int argc, char** argv) {
    int frames = 600;
    if (argc > 2) frames = std::atoi(argv[2]);
    if (frames <= 0 || frames > 100000) {
        std::cerr << "normbench arguments out of range\n";
        return 1;
    }
    std::string error;
    struct Case { const char* name; int width; int height; int rotation; };
    const Case cases[] = {
        {"identity", 1280, 720, 0},
        {"letterbox", 960, 540, 0},
        {"rotate90", 1280, 720, 90},
    };
    CVPixelBufferPoolRef pool = MakeNormPool(2, error);
    if (!pool) {
        std::cerr << error << "\n";
        return 1;
    }
    std::cout << "normbench frames=" << frames << "\n";
    MetalNormalize metalState;
    bool metalReady = false;
    for (const Case& c : cases) {
        km::mac::PixelBuffer input = MakeNormInput(c.width, c.height,
            kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, error);
        if (!input) {
            std::cerr << error << "\n";
            CFRelease(pool);
            return 1;
        }
        std::cout << "case " << c.name << " input=" << c.width << "x" << c.height
                  << " strideY=" << CVPixelBufferGetBytesPerRowOfPlane(input.get(), 0)
                  << " rotation=" << c.rotation << "\n";
        for (int i = 0; i < 5; ++i) {
            km::mac::PixelBuffer warm = km::mac::Normalize720p(input.get(), c.rotation, error);
            if (!warm) {
                std::cerr << "baseline warmup: " << error << "\n";
                CFRelease(pool);
                return 1;
            }
            km::mac::PixelBuffer warmPool =
                km::mac::Normalize720p(input.get(), c.rotation, error, pool);
            if (!warmPool) {
                std::cerr << "pool warmup: " << error << "\n";
                CFRelease(pool);
                return 1;
            }
        }
        NormSample baseline, pooled;
        if (!MeasureNormalize(input.get(), c.rotation, frames, false, pool, baseline, error)) {
            std::cerr << "baseline: " << error << "\n";
            CFRelease(pool);
            return 1;
        }
        if (!MeasureNormalize(input.get(), c.rotation, frames, true, pool, pooled, error)) {
            std::cerr << "pool: " << error << "\n";
            CFRelease(pool);
            return 1;
        }
        PrintNormSample("baseline", baseline);
        PrintNormSample("pool    ", pooled);
        if (baseline.reusesInput) {
            std::cout << "  metal   not-applicable (identity path hands the input back)\n";
            continue;
        }
        if (!metalReady && !MakeMetalNormalize(metalState, error)) {
            std::cerr << error << "\n";
            CFRelease(pool);
            return 1;
        }
        metalReady = true;
        MetalSample metal;
        if (!MeasureMetal(metalState, input.get(), c.rotation, frames, metal, error)) {
            std::cerr << "metal: " << error << "\n";
            CFRelease(pool);
            return 1;
        }
        // Correctness: the GPU result must equal the CPU reference byte for
        // byte, otherwise the timing compares two different transforms.
        {
            km::mac::PixelBuffer cpuOut = km::mac::Normalize720p(input.get(), c.rotation, error);
            CVPixelBufferRef raw = nullptr;
            NSDictionary* attrs = @{
                (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{},
                (__bridge NSString*)kCVPixelBufferMetalCompatibilityKey: @YES
            };
            const CVReturn status = CVPixelBufferCreate(kCFAllocatorDefault, 1280, 720,
                kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,
                (__bridge CFDictionaryRef)attrs, &raw);
            if (!cpuOut || status != kCVReturnSuccess) {
                std::cerr << "diff setup: " << error << "\n";
                CFRelease(pool);
                return 1;
            }
            km::mac::PixelBuffer gpuOut(raw);
            double gpuUs = -1;
            if (!MetalNormalizeOnce(metalState, input.get(), gpuOut.get(), c.rotation, gpuUs,
                                    error)) {
                std::cerr << "metal diff run: " << error << "\n";
                CFRelease(pool);
                return 1;
            }
            metal.diffBytes = DiffByteCount(cpuOut.get(), gpuOut.get());
        }
        PrintMetalSample("metal   ", metal);
    }
    // A format change is refused with a reason rather than converted silently.
    {
        km::mac::PixelBuffer fullRange = MakeNormInput(1280, 720,
            kCVPixelFormatType_420YpCbCr8BiPlanarFullRange, error);
        error.clear();
        km::mac::PixelBuffer rejected =
            fullRange ? km::mac::Normalize720p(fullRange.get(), 0, error) : km::mac::PixelBuffer();
        std::cout << "format-change fullRange accepted=" << (rejected ? 1 : 0)
                  << " reason=" << (error.empty() ? "(none)" : error) << "\n";
        if (rejected) {
            std::cerr << "full range input must not be accepted\n";
            CFRelease(pool);
            return 1;
        }
    }
    // Pool exhaustion: hold one buffer out of a pool capped at one, then ask
    // for another with an allocation threshold. The pool must refuse instead
    // of growing past its cap.
    {
        std::string poolError;
        CVPixelBufferPoolRef small = MakeNormPool(1, poolError);
        if (!small) {
            std::cerr << poolError << "\n";
            CFRelease(pool);
            return 1;
        }
        CVPixelBufferRef heldRaw = nullptr;
        const CVReturn held = CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, small, &heldRaw);
        km::mac::PixelBuffer heldBuffer(heldRaw);
        NSDictionary* aux = @{
            (__bridge NSString*)kCVPixelBufferPoolAllocationThresholdKey: @1
        };
        CVPixelBufferRef extraRaw = nullptr;
        const CVReturn refused = CVPixelBufferPoolCreatePixelBufferWithAuxAttributes(
            kCFAllocatorDefault, small, (__bridge CFDictionaryRef)aux, &extraRaw);
        km::mac::PixelBuffer extraBuffer(extraRaw);
        std::cout << "pool-exhaustion held=" << held << " refill=" << refused
                  << " extraBuffer=" << (extraBuffer ? 1 : 0) << "\n";
        CFRelease(small);
    }
    CFRelease(pool);
    std::cout << "normbench done\n";
    return 0;
}

// Stage-8 S1 baseline: feeds pre-encoded 720p H.264 through the pipeline and
// prints the timing counters, so the same numbers can be compared before and
// after the decoder changes. Not registered with ctest; run it by hand and keep
// the output as the record.
//
//   km_macos_pipeline_test --bench [frames] [fps] [rotation]
//     frames  encoded AUs to feed (default 300)
//     fps     submit rate; 0 submits everything as fast as possible
//     rotation 0/90/180/270, so the letterbox path can be timed too
int RunBench(int argc, char** argv) {
    int frames = 300, fps = 30, rotation = 0;
    if (argc > 2) frames = std::atoi(argv[2]);
    if (argc > 3) fps = std::atoi(argv[3]);
    if (argc > 4) rotation = std::atoi(argv[4]);
    if (frames <= 0 || fps < 0 || (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270)) {
        std::cerr << "bench arguments out of range\n";
        return 1;
    }
    constexpr int kGop = 60;
    const size_t capacity = fps > 0 ? 8 : size_t(frames);

    Encoder encoder;
    if (!encoder.ok()) {
        std::cerr << encoder.error() << "\n";
        return 1;
    }
    // Encoding stays out of the timed window: only submit() pacing and the
    // pipeline's own work are measured.
    std::vector<EncodedAu> aus;
    aus.resize(size_t(frames));
    for (int i = 0; i < frames; ++i) {
        std::string encodeError;
        if (!encoder.encode(i, i % kGop == 0, aus[size_t(i)], encodeError)) {
            std::cerr << "encode frame " << i << ": " << encodeError << "\n";
            return 1;
        }
    }

    // A decode() that returns no image and no error is a configuration-only AU
    // by contract, so classify the AUs before feeding them: that is the only
    // other explanation for a frame the worker does not publish.
    int withVcl = 0, withoutVcl = 0, withParameterSets = 0;
    for (size_t index = 0; index < aus.size(); ++index) {
        const EncodedAu& au = aus[index];
        std::vector<Bytes> nalus;
        bool hasVcl = false, hasParameterSets = false;
        if (km::h264::SplitAnnexB(au.annexB, nalus)) {
            for (Bytes nalu : nalus) {
                const uint8_t type = uint8_t(nalu[0]) & 31;
                if (type == 1 || type == 5) hasVcl = true;
                if (type == 7 || type == 8) hasParameterSets = true;
            }
        }
        if (hasVcl) ++withVcl;
        else {
            ++withoutVcl;
            if (withoutVcl <= 5) {
                std::cout << "ausWithoutVcl index=" << index << " bytes=" << au.annexB.size()
                          << " types=";
                for (Bytes nalu : nalus) std::cout << int(uint8_t(nalu[0]) & 31) << ",";
                std::cout << "\n  hex=";
                for (size_t i = 0; i < au.annexB.size() && i < 48; ++i) {
                    const char* digits = "0123456789abcdef";
                    std::cout << digits[au.annexB[i] >> 4] << digits[au.annexB[i] & 15];
                }
                std::cout << "\n";
            }
        }
        if (hasParameterSets) ++withParameterSets;
    }

    VideoPipeline pipe([](CVPixelBufferRef) {}, capacity);
    std::string error;
    OutputFormat format;
    if (fps > 0) format.fpsNumerator = uint32_t(fps);
    if (!pipe.start(format, 1, error)) {
        std::cerr << error << "\n";
        return 1;
    }
    pipe.setTransform({rotation});

    int accepted = 0, backpressure = 0, needKeyframe = 0, rejected = 0;
    const auto sendStart = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) {
        const bool idr = i % kGop == 0;
        switch (pipe.submit(frameFrom(aus[size_t(i)], idr, 1, i))) {
        case SubmitResult::Accepted: ++accepted; break;
        case SubmitResult::Backpressure: ++backpressure; break;
        case SubmitResult::NeedKeyframe: ++needKeyframe; break;
        default: ++rejected; break;
        }
        if (fps > 0)
            std::this_thread::sleep_until(
                sendStart + std::chrono::microseconds(int64_t(i + 1) * 1000000 / fps));
    }
    const auto sendEnd = std::chrono::steady_clock::now();
    // Rejected submissions never reach the worker, so the worker is drained
    // once it has executed one decode() per accepted AU.
    const bool drained =
        waitUntil([&] { return pipe.stats().decodeUsCount >= uint64_t(accepted); }, 60000);
    const auto drainEnd = std::chrono::steady_clock::now();
    const km::mac::PipelineStats stats = pipe.stats();
    pipe.stop();

    const auto elapsedMs = [](std::chrono::steady_clock::time_point a,
                              std::chrono::steady_clock::time_point b) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
    };
    const auto avg = [](uint64_t total, uint64_t count) {
        return count ? double(total) / double(count) : 0.0;
    };
    std::cout << "bench frames=" << frames << " fps=" << fps << " rotation=" << rotation
              << " mode=" << (fps > 0 ? "paced" : "burst") << " capacity=" << capacity
              << " nalLengthSize=" << encoder.nalLengthSize() << "\n"
              << "ausWithVcl=" << withVcl << " ausWithoutVcl=" << withoutVcl
              << " ausWithParameterSets=" << withParameterSets << "\n"
              << "accepted=" << accepted << " backpressure=" << backpressure
              << " needKeyframe=" << needKeyframe << " rejected=" << rejected << "\n"
              << "published=" << stats.published << " decodeErrors=" << stats.decodeErrors
              << " normalizeErrors=" << stats.normalizeErrors << " drained=" << drained << "\n"
              << "queueHighWater=" << stats.queueHighWater << "\n"
              << "decodeUs avg=" << avg(stats.decodeUsTotal, stats.decodeUsCount)
              << " max=" << stats.decodeUsMax << " n=" << stats.decodeUsCount << "\n"
              << "toPublishUs avg=" << avg(stats.submitToPublishUsTotal, stats.submitToPublishUsCount)
              << " max=" << stats.submitToPublishUsMax << " n=" << stats.submitToPublishUsCount << "\n"
              << "workerResidualUs avg="
              << avg(stats.submitToPublishUsTotal - stats.decodeUsTotal, stats.submitToPublishUsCount)
              << "\n"
              << "sendWallMs=" << elapsedMs(sendStart, sendEnd)
              << " drainWallMs=" << elapsedMs(sendEnd, drainEnd)
              << " totalWallMs=" << elapsedMs(sendStart, drainEnd) << "\n";
    return (drained && stats.published == uint64_t(frames) && stats.decodeErrors == 0 &&
            stats.normalizeErrors == 0 && stats.submitToPublishUsCount == stats.published)
        ? 0
        : 1;
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--bench") return RunBench(argc, argv);
    if (argc > 1 && std::string(argv[1]) == "--decodeprobe") return RunDecodeProbe(argc, argv);
    if (argc > 1 && std::string(argv[1]) == "--normbench") return RunNormBench(argc, argv);
    Encoder encoder;
    if (!encoder.ok()) {
        std::cerr << encoder.error() << "\n";
        return 1;
    }
    std::string encodeError;
    EncodedAu idr, pframe;
    if (!encoder.encode(0, true, idr, encodeError)) {
        std::cerr << encodeError << "\n";
        return 1;
    }
    CHECK(!idr.annexB.empty());
    CHECK(idr.keyframe);
    if (!encoder.encode(1, false, pframe, encodeError)) {
        std::cerr << encodeError << "\n";
        return 1;
    }
    CHECK(!pframe.annexB.empty());
    CHECK(!pframe.keyframe);

    // start() validation and submit before start.
    {
        VideoPipeline pipe([](CVPixelBufferRef) {}, 4);
        std::string error;
        OutputFormat small;
        small.width = 640;
        small.height = 360;
        CHECK(!pipe.start(small, 1, error));
        CHECK(!error.empty());
        OutputFormat zeroFps;
        zeroFps.fpsNumerator = 0;
        CHECK(!pipe.start(zeroFps, 1, error));
        CHECK(!error.empty());
        CHECK(pipe.submit(frameFrom(idr, true, 1, 0)) == SubmitResult::Stopped);
    }

    // Full flow: stale generation, malformed AU, NeedKeyframe before any IDR,
    // 90° transform delivery at 720p 420v, P frame after IDR, stop semantics.
    {
        Sink sink;
        VideoPipeline pipe([&](CVPixelBufferRef buffer) { sink(buffer); }, 4);
        std::string error;
        CHECK(pipe.start({}, 7, error));
        CHECK(error.empty());

        CHECK(pipe.submit(frameFrom(idr, true, 6, 0)) == SubmitResult::Stopped);
        CHECK(pipe.stats().staleGeneration == 1);

        EncodedVideoFrame malformed;
        malformed.annexB = {1, 2, 3};
        malformed.generation = 7;
        CHECK(pipe.submit(std::move(malformed)) == SubmitResult::Error);
        CHECK(pipe.stats().rejected == 1);

        CHECK(pipe.submit(frameFrom(pframe, false, 7, 1)) == SubmitResult::NeedKeyframe);
        CHECK(pipe.stats().needKeyframe == 1);

        pipe.setTransform({90});
        CHECK(pipe.submit(frameFrom(idr, true, 7, 2)) == SubmitResult::Accepted);
        CHECK(waitUntil([&] { return pipe.stats().published >= 1; }));
        CHECK(pipe.submit(frameFrom(pframe, false, 7, 3)) == SubmitResult::Accepted);
        CHECK(waitUntil([&] { return pipe.stats().published >= 2; }));

        {
            std::lock_guard lock(sink.mutex);
            CHECK(sink.entered >= 2);
            CHECK(sink.width == kWidth);
            CHECK(sink.height == kHeight);
            CHECK(sink.pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange);
        }
        CHECK(pipe.stats().decodeErrors == 0);
        CHECK(pipe.stats().normalizeErrors == 0);
        // Stage-8 timing counters: every delivered frame contributes exactly
        // one latency sample, and the queue is never empty right after a push.
        CHECK(pipe.stats().submitToPublishUsCount == pipe.stats().published);
        CHECK(pipe.stats().decodeUsCount >= pipe.stats().published);
        CHECK(pipe.stats().queueHighWater >= 1);

        pipe.stop();
        const uint64_t frozen = pipe.stats().published;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(pipe.stats().published == frozen);
        CHECK(pipe.submit(frameFrom(idr, true, 7, 4)) == SubmitResult::Stopped);
    }

    // Decode failure (SPS without PPS) re-arms NeedKeyframe; a fresh IDR recovers.
    {
        Sink sink;
        VideoPipeline pipe([&](CVPixelBufferRef buffer) { sink(buffer); }, 4);
        std::string error;
        CHECK(pipe.start({}, 8, error));

        std::vector<Bytes> parts;
        CHECK(km::h264::SplitAnnexB(idr.annexB, parts));
        CHECK(parts.size() >= 2);
        EncodedVideoFrame broken;
        AppendAnnexB(broken.annexB, parts[0]); // SPS only: decoder requires the pair
        broken.generation = 8;
        broken.randomAccess = true;
        CHECK(pipe.submit(std::move(broken)) == SubmitResult::Accepted);
        CHECK(waitUntil([&] { return pipe.stats().decodeErrors >= 1; }));

        CHECK(pipe.submit(frameFrom(pframe, false, 8, 1)) == SubmitResult::NeedKeyframe);
        CHECK(pipe.submit(frameFrom(idr, true, 8, 2)) == SubmitResult::Accepted);
        CHECK(waitUntil([&] { return pipe.stats().published >= 1; }));
        pipe.stop();
    }

    // Backpressure with the worker held inside the handler: the queue fills,
    // the dependency chain is discarded, only a later IDR flows again.
    {
        Sink sink;
        sink.block = true;
        VideoPipeline pipe([&](CVPixelBufferRef buffer) { sink(buffer); }, 2);
        std::string error;
        CHECK(pipe.start({}, 9, error));

        CHECK(pipe.submit(frameFrom(idr, true, 9, 0)) == SubmitResult::Accepted);
        CHECK(sink.waitEntered(1)); // worker parked in the handler; queue empty
        CHECK(pipe.submit(frameFrom(pframe, false, 9, 1)) == SubmitResult::Accepted);
        CHECK(pipe.submit(frameFrom(pframe, false, 9, 2)) == SubmitResult::Accepted);
        CHECK(pipe.submit(frameFrom(pframe, false, 9, 3)) == SubmitResult::Backpressure);
        CHECK(pipe.stats().backpressure == 1);
        CHECK(pipe.stats().queueHighWater >= 2);
        CHECK(pipe.submit(frameFrom(pframe, false, 9, 4)) == SubmitResult::NeedKeyframe);
        CHECK(pipe.submit(frameFrom(idr, true, 9, 5)) == SubmitResult::Accepted);

        sink.openGate();
        CHECK(waitUntil([&] { return pipe.stats().published >= 2; }));
        CHECK(pipe.stats().decodeErrors == 0);
        pipe.stop();
    }

    // stop() invalidates queued work before returning and never runs the
    // handler afterwards, even while the worker was parked mid-frame.
    {
        Sink sink;
        sink.block = true;
        VideoPipeline pipe([&](CVPixelBufferRef buffer) { sink(buffer); }, 4);
        std::string error;
        CHECK(pipe.start({}, 10, error));

        CHECK(pipe.submit(frameFrom(idr, true, 10, 0)) == SubmitResult::Accepted);
        CHECK(sink.waitEntered(1));
        CHECK(pipe.submit(frameFrom(pframe, false, 10, 1)) == SubmitResult::Accepted);
        CHECK(pipe.submit(frameFrom(idr, true, 10, 2)) == SubmitResult::Accepted);

        std::thread stopper([&] { pipe.stop(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(150)); // queue invalidated, join pending
        sink.openGate();
        stopper.join();

        CHECK(pipe.stats().published == 1); // queued frames never ran
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(pipe.stats().published == 1);
        CHECK(pipe.submit(frameFrom(idr, true, 10, 3)) == SubmitResult::Stopped);
    }

    // Stage-8 S2: bounded outstanding decode. The window never grows past its
    // bound, every result leaves in submission order, and reset() with a full
    // window drains the outstanding callbacks instead of hanging or losing one.
    {
        std::string error;
        // One coherent chain: the stored IDR, the stored P frame, then the six
        // P frames the encoder produced right after them.
        std::vector<EncodedAu> windowAus{idr, pframe};
        for (int i = 2; i <= 7; ++i) {
            EncodedAu au;
            CHECK(encoder.encode(i, false, au, error));
            CHECK(!au.annexB.empty());
            windowAus.push_back(std::move(au));
        }

        VideoToolboxDecoder decoder;
        std::vector<int64_t> submittedPts;
        size_t next = 0, taken = 0, maxOutstanding = 0;

        // More AUs are waiting than the window can hold: submission stops at
        // the bound and the owner takes results instead of queueing more.
        CHECK(FillWindow(decoder, windowAus, next, submittedPts, maxOutstanding, error));
        CHECK(next == VideoToolboxDecoder::kMaxOutstanding);
        CHECK(maxOutstanding <= VideoToolboxDecoder::kMaxOutstanding);
        while (decoder.outstanding() > 0)
            CHECK(TakeOldestInOrder(decoder, submittedPts, taken, error));
        CHECK(taken == next);

        // reset() with four AUs still decoding: the session is drained and
        // invalidated, and each slot still comes back with its own frame.
        CHECK(FillWindow(decoder, windowAus, next, submittedPts, maxOutstanding, error));
        CHECK(decoder.outstanding() == VideoToolboxDecoder::kMaxOutstanding);
        CHECK(maxOutstanding <= VideoToolboxDecoder::kMaxOutstanding);
        decoder.reset();
        while (decoder.outstanding() > 0)
            CHECK(TakeOldestInOrder(decoder, submittedPts, taken, error));
        CHECK(next == windowAus.size());
        CHECK(taken == windowAus.size());
    }

    std::cout << "video_pipeline: all checks passed\n";
    return 0;
}
