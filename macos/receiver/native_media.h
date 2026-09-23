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
// Dedicated owner thread only. This foundation waits per AU, not a pipelined decoder.
// Do not call from the UI thread, an RTC callback or the Camera Extension.
class VideoToolboxDecoder {
public:
    VideoToolboxDecoder();
    ~VideoToolboxDecoder();
    VideoToolboxDecoder(const VideoToolboxDecoder&) = delete;
    VideoToolboxDecoder& operator=(const VideoToolboxDecoder&) = delete;
    PixelBuffer decode(std::span<const uint8_t> annexB, int64_t presentationUs, std::string& error);
    void reset();
    bool hardwareActive() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace km::mac
