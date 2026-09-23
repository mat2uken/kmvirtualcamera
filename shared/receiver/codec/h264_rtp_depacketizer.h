#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>
#include "../../km/rtp_wire.h"

namespace km::codec {
// Single serial owner. Late/duplicate RTP packets are discarded, not reordered.
// A gap invalidates the dependent AU chain until an intact IDR arrives.
class H264RtpDepacketizer {
public:
    using FrameCallback = std::function<void(const uint8_t*, size_t, uint32_t)>;
    using KeyframeRequestCallback = std::function<void()>;
    static constexpr size_t kMaxAccessUnitBytes = 4 * 1024 * 1024;
    explicit H264RtpDepacketizer(FrameCallback cb = {}, KeyframeRequestCallback key = {});
    void SetCallback(FrameCallback cb) { callback_ = std::move(cb); }
    void SetKeyframeRequestCallback(KeyframeRequestCallback cb) { keyframe_ = std::move(cb); }
    void Reset();
    void ProcessRtpPacket(const uint8_t* bytes, size_t size);
private:
    void lose();
    void emit();
    bool appendNalu(km::wire::Bytes nal);
    void clearAu();
    FrameCallback callback_;
    KeyframeRequestCallback keyframe_;
    std::vector<uint8_t> au_, fu_, sps_, pps_;
    uint32_t timestamp_ = 0, ssrc_ = 0;
    uint16_t sequence_ = 0;
    uint8_t fuHeader_ = 0;
    bool haveSsrc_ = false, haveSequence_ = false, haveTimestamp_ = false;
    bool waiting_ = true, damaged_ = false, idr_ = false, vcl_ = false;
    bool hasSps_ = false, hasPps_ = false;
};
} // namespace km::codec
