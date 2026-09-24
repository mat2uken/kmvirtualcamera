#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "../../km/receiver_contracts.h"
#include "../../km/timing.h"
#include "../codec/dc_video_depacketizer.h"
#include "../codec/h264_rtp_depacketizer.h"

namespace km::engine {
// Which media path delivered the newest access unit (RTP wins until the first DC AU).
enum class MediaPath { Rtp, DataChannel };
// Route for one keyframe request: RTCP PLI or {"type":"pli"} on the control channel.
enum class KeyframeChannel { Rtcp, DataChannel };

struct ReceiverStats {
    uint64_t rtpPackets = 0;         // RTP packets offered to the engine
    uint64_t rtpPacketsDropped = 0;  // parse/payload-type rejection or RTP while DC is active
    uint64_t dataChannelPackets = 0; // DC video packets offered to the depacketizer
    uint64_t rtpFrames = 0;
    uint64_t dataChannelFrames = 0;
    uint64_t recoverySignals = 0;    // RTP depacketizer reported loss or damage
    uint64_t keyframeRequestsDispatched = 0; // passed the 50ms throttle
    uint64_t controlMessagesOut = 0; // control messages emitted by the DC depacketizer
    uint64_t lastFrameArrivalNs = 0;
};

// Receiver policy consolidated from windows/receiver/rtc/peer_connection_manager.*:
// payload-type filter (fail-closed), SSRC/timestamp unwrap resets, RTP-vs-DC path
// arbitration, 50ms-throttled keyframe requests with timer retry until a configured IDR,
// and owning EncodedVideoFrame delivery (RTP: unwrapped 90kHz ticks; DC: unwrapped sender
// microseconds; arrival and generation stamped per frame).
//
// Single serial owner: invoke every method from one thread, never concurrently.
// Callbacks run inline on that thread. Handlers must not throw and must not re-enter
// process*/onTimer/onDataChannelVideoClosed/reset; requestKeyframe and the query methods
// are safe from a handler. The constructor wires depacketizer callbacks that capture
// `this`, so the engine is neither copyable nor movable.
// Platform-free by design: no Win32/D3D/CVPixelBuffer/rtc:: types and no bandwidth
// estimator - loss notifications surface through setLossHandler for the platform to own.
class ReceiverEngine {
public:
    using FrameHandler = std::function<void(const EncodedVideoFrame&)>;
    using KeyframeRequestHandler = std::function<void(KeyframeChannel)>;
    using ControlSendHandler = std::function<void(const std::string&)>;
    using LossHandler = std::function<void(uint64_t nowNs)>;

    ReceiverEngine();
    ReceiverEngine(const ReceiverEngine&) = delete;
    ReceiverEngine& operator=(const ReceiverEngine&) = delete;

    void setFrameHandler(FrameHandler handler) { frameHandler_ = std::move(handler); }
    void setKeyframeRequestHandler(KeyframeRequestHandler handler) { keyframeRequestHandler_ = std::move(handler); }
    void setControlSendHandler(ControlSendHandler handler) { controlSendHandler_ = std::move(handler); }
    void setLossHandler(LossHandler handler) { lossHandler_ = std::move(handler); }
    // Fail-closed: an empty set drops every RTP packet (mirrors the SDP H264/90kHz check).
    void setVideoPayloadTypes(std::vector<uint8_t> types) { payloadTypes_ = std::move(types); }
    void setGeneration(uint64_t generation) { generation_ = generation; }
    uint64_t generation() const { return generation_; }
    // The video DataChannel is open; keyframe requests route to the control channel.
    void onVideoDataChannelOpened() { videoDcOpen_ = true; }

    // All times are monotonic nanoseconds supplied by the platform (the engine has no clock).
    void processRtpVideoPacket(const uint8_t* data, size_t size, uint64_t arrivalNs);
    void processDataChannelPacket(const uint8_t* data, size_t size, uint64_t arrivalNs);
    void onTimer(uint64_t nowNs);
    // Video DataChannel closed: drops both paths' packet state and re-arms keyframe recovery.
    void onDataChannelVideoClosed();
    void requestKeyframe(uint64_t nowNs);
    // Session teardown: full packet and keyframe state reset. Handlers, payload types,
    // generation and cumulative stats are retained.
    void reset();

    MediaPath activePath() const { return dataChannelVideo_ ? MediaPath::DataChannel : MediaPath::Rtp; }
    bool needsKeyframe() const { return needKeyframe_; }
    const ReceiverStats& stats() const { return stats_; }

private:
    static constexpr int64_t kKeyframeThrottleNs = 50'000'000; // PCM SendKeyframeRequest throttle
    void emitFrame(const uint8_t* data, size_t size, int64_t mediaTicks, TimestampDomain domain, bool fromDataChannel);
    void tryDispatchKeyframeRequest(uint64_t nowNs);

    codec::H264RtpDepacketizer rtpDepacketizer_;
    codec::DcVideoDepacketizer dcDepacketizer_;
    TimestampUnwrapper32 rtpUnwrapper_, dcUnwrapper_;
    std::vector<uint8_t> payloadTypes_;
    FrameHandler frameHandler_;
    KeyframeRequestHandler keyframeRequestHandler_;
    ControlSendHandler controlSendHandler_;
    LossHandler lossHandler_;
    ReceiverStats stats_;
    uint64_t generation_ = 0;
    uint64_t currentArrivalNs_ = 0;
    int64_t lastKeyframeRequestNs_ = -1;
    uint32_t videoSsrc_ = 0;
    bool haveVideoSsrc_ = false;
    bool dataChannelVideo_ = false;
    bool videoDcOpen_ = false;
    bool needKeyframe_ = true;
};
} // namespace km::engine
