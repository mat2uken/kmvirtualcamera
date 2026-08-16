#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <span>

namespace km::codec {

class H264RtpDepacketizer {
public:
    using FrameCallback = std::function<void(const uint8_t* nalData, size_t size, uint32_t timestamp)>;
    using KeyframeRequestCallback = std::function<void()>;

    explicit H264RtpDepacketizer(FrameCallback callback = nullptr, KeyframeRequestCallback keyframeCb = nullptr);

    void SetCallback(FrameCallback callback) {
        callback_ = std::move(callback);
    }

    void SetKeyframeRequestCallback(KeyframeRequestCallback keyframeCb) {
        keyframeRequestCallback_ = std::move(keyframeCb);
    }

    void Reset();

    // Process a raw RTP packet buffer from WebRTC track
    void ProcessRtpPacket(const uint8_t* rtpData, size_t size);

private:
    void EmitAccessUnit();

    FrameCallback callback_;
    KeyframeRequestCallback keyframeRequestCallback_;
    std::vector<uint8_t> accessUnitBuffer_;
    std::vector<uint8_t> fuBuffer_;
    std::vector<uint8_t> cachedSps_;
    std::vector<uint8_t> cachedPps_;
    std::vector<uint8_t> reconstructedFrameBuffer_;
    uint32_t currentTimestamp_{0};
    uint16_t lastSequenceNumber_{0};
    bool hasPendingTimestamp_{false};
    bool hasLastSeq_{false};
    bool isFuActive_{false};
    bool frameHasLoss_{false};
    bool isKeyframe_{false};
    bool waitingForKeyframe_{true};
    uint32_t waitingKeyframeCount_{0};
};

} // namespace km::codec
