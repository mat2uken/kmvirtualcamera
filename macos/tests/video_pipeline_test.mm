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
#include <VideoToolbox/VideoToolbox.h>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
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

    std::cout << "video_pipeline: all checks passed\n";
    return 0;
}
