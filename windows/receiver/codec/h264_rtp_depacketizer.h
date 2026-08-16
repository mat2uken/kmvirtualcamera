#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <span>

namespace km::codec {

class H264RtpDepacketizer {
public:
    using FrameCallback = std::function<void(const uint8_t* nalData, size_t size, uint32_t timestamp)>;

    explicit H264RtpDepacketizer(FrameCallback callback = nullptr);

    void SetCallback(FrameCallback callback) {
        callback_ = std::move(callback);
    }

    void Reset();

    // Process a raw RTP packet buffer from WebRTC track
    void ProcessRtpPacket(const uint8_t* rtpData, size_t size);

private:
    void EmitAccessUnit();

    FrameCallback callback_;
    std::vector<uint8_t> accessUnitBuffer_;
    std::vector<uint8_t> fuBuffer_;
    uint32_t currentTimestamp_{0};
    bool hasPendingTimestamp_{false};
    bool isFuActive_{false};
};

} // namespace km::codec
