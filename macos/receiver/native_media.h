#pragma once
#include <CoreVideo/CoreVideo.h>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>

namespace km::mac {
// Constructor adopts a +1 reference. retain() explicitly retains a borrowed reference.
class PixelBuffer {
public:
    PixelBuffer() = default;
    explicit PixelBuffer(CVPixelBufferRef owned) : value_(owned) {}
    static PixelBuffer retain(CVPixelBufferRef borrowed) {
        if (borrowed) CVPixelBufferRetain(borrowed);
        return PixelBuffer(borrowed);
    }
    PixelBuffer(const PixelBuffer& other) : PixelBuffer(retain(other.value_)) {}
    PixelBuffer(PixelBuffer&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    PixelBuffer& operator=(PixelBuffer other) noexcept { std::swap(value_, other.value_); return *this; }
    ~PixelBuffer() { if (value_) CVPixelBufferRelease(value_); }
    CVPixelBufferRef get() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }
private:
    CVPixelBufferRef value_ = nullptr;
};
PixelBuffer MakeBlack720p(std::string& error);
PixelBuffer Normalize720p(CVPixelBufferRef input, int clockwiseRotation, std::string& error);
// Owner thread only. Do not call from the UI thread, an RTC callback or the
// Camera Extension.
//
// Stage 8 S2: the decoder keeps a bounded window of outstanding AUs instead of
// waiting after every AU. submit() enqueues into the window, takeOldest()
// returns results in submission order, which is presentation order, so a
// completion that runs ahead of an earlier frame never overtakes it. The window
// is bounded (kMaxOutstanding) so inputs are never held without limit.
// Every callback carries the session generation: when a session is drained and
// invalidated, its generation is retired and a late callback is ignored.
// reset() retires the session but never drops a slot, because the owner pairs
// every queued submission with exactly one takeOldest().
class VideoToolboxDecoder {
public:
    // Outstanding AUs the owner may submit before it must take a result.
    static constexpr size_t kMaxOutstanding = 4;
    enum class SubmitOutcome {
        Queued,   // a result will arrive through takeOldest()
        NoOutput, // configuration-only AU: nothing to take
        Failed,   // error is set; nothing to take
    };
    VideoToolboxDecoder();
    ~VideoToolboxDecoder();
    VideoToolboxDecoder(const VideoToolboxDecoder&) = delete;
    VideoToolboxDecoder& operator=(const VideoToolboxDecoder&) = delete;
    SubmitOutcome submit(std::span<const uint8_t> annexB, int64_t presentationUs,
                         std::string& error);
    // Blocks until the oldest submitted AU completes, then removes it. Sets
    // error when that AU failed to decode; an empty result with an empty error
    // means the decoder produced no image for it (dropped frame). When
    // presentationUs is given it receives the pts that AU was submitted with,
    // which is how a caller confirms results left in submission order.
    void takeOldest(PixelBuffer& out, std::string& error, int64_t* presentationUs = nullptr);
    size_t outstanding() const;
    // One-AU-at-a-time helper for tools and probes: submit then takeOldest.
    PixelBuffer decode(std::span<const uint8_t> annexB, int64_t presentationUs, std::string& error);
    void reset();
    bool hardwareActive() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace km::mac
