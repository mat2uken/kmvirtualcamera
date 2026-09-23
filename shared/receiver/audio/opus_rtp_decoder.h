#pragma once
#include "../../km/rtp_wire.h"
#include <functional>
#include <map>
#include <memory>
#include <vector>
namespace km::audio {
// Serial RTC owner and its 20ms timer only. PCM callback size is the number of
// INTERLEAVED int16 elements (never multiply by channels a second time).
class OpusRtpDecoder {
public:
    using Callback = std::function<void(const int16_t*, size_t, int, int)>;
    OpusRtpDecoder();
    ~OpusRtpDecoder();
    OpusRtpDecoder(const OpusRtpDecoder&) = delete;
    OpusRtpDecoder& operator=(const OpusRtpDecoder&) = delete;
    bool Configure(uint8_t payloadType, Callback callback);
    void Reset();
    void Receive(wire::Bytes bytes, int64_t nowUs);
    void Tick(int64_t nowUs);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace km::audio
