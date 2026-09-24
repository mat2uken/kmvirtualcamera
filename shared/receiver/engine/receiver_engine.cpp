#include "receiver_engine.h"
#include "../../km/bounded_video_queue.h"
#include "../../km/rtp_wire.h"
#include <algorithm>

namespace km::engine {

ReceiverEngine::ReceiverEngine() {
    rtpDepacketizer_.SetCallback([this](const uint8_t* data, size_t size, uint32_t ticks) {
        if (dataChannelVideo_) return; // belt-and-braces: the feed is already gated below
        emitFrame(data, size, rtpUnwrapper_.unwrap(ticks), TimestampDomain::Rtp90kHz, false);
    });
    rtpDepacketizer_.SetKeyframeRequestCallback([this] {
        ++stats_.recoverySignals;
        if (lossHandler_) lossHandler_(currentArrivalNs_);
        needKeyframe_ = true;
        tryDispatchKeyframeRequest(currentArrivalNs_);
    });
    dcDepacketizer_.SetCallback([this](const uint8_t* data, size_t size, int64_t raw) {
        dataChannelVideo_ = true; // the first DC AU wins the path
        emitFrame(data, size, dcUnwrapper_.unwrap(uint32_t(raw)), TimestampDomain::SenderMicroseconds, true);
    });
    dcDepacketizer_.SetControlSendCallback([this](const std::string& text) {
        ++stats_.controlMessagesOut;
        if (controlSendHandler_) controlSendHandler_(text);
    });
}

void ReceiverEngine::processRtpVideoPacket(const uint8_t* data, size_t size, uint64_t arrivalNs) {
    currentArrivalNs_ = arrivalNs;
    ++stats_.rtpPackets;
    const auto packet = km::wire::parseRtp({data, size});
    if (!packet || std::find(payloadTypes_.begin(), payloadTypes_.end(), packet->payloadType) == payloadTypes_.end()) {
        ++stats_.rtpPacketsDropped; // fail-closed: an empty payload-type set drops all RTP
        return;
    }
    if (!haveVideoSsrc_ || videoSsrc_ != packet->ssrc) {
        rtpUnwrapper_.reset(); // a new SSRC starts a new 90kHz timestamp domain
        videoSsrc_ = packet->ssrc;
        haveVideoSsrc_ = true;
    }
    if (dataChannelVideo_) { ++stats_.rtpPacketsDropped; return; }
    rtpDepacketizer_.ProcessRtpPacket(data, size);
}

void ReceiverEngine::processDataChannelPacket(const uint8_t* data, size_t size, uint64_t arrivalNs) {
    currentArrivalNs_ = arrivalNs;
    ++stats_.dataChannelPackets;
    dcDepacketizer_.ProcessPacketAt({data, size}, int64_t(arrivalNs / 1000));
}

void ReceiverEngine::onTimer(uint64_t nowNs) {
    currentArrivalNs_ = nowNs;
    dcDepacketizer_.OnTimerTickAt(int64_t(nowNs / 1000));
    if (needKeyframe_) tryDispatchKeyframeRequest(nowNs);
}

void ReceiverEngine::onDataChannelVideoClosed() {
    dataChannelVideo_ = false;
    videoDcOpen_ = false;
    dcDepacketizer_.Reset();
    dcUnwrapper_.reset();
    rtpDepacketizer_.Reset();
    rtpUnwrapper_.reset();
    needKeyframe_ = true;
}

void ReceiverEngine::requestKeyframe(uint64_t nowNs) {
    needKeyframe_ = true;
    tryDispatchKeyframeRequest(nowNs);
}

void ReceiverEngine::reset() {
    onDataChannelVideoClosed();
    haveVideoSsrc_ = false;
    videoSsrc_ = 0;
    lastKeyframeRequestNs_ = -1;
}

void ReceiverEngine::tryDispatchKeyframeRequest(uint64_t nowNs) {
    const int64_t now = int64_t(nowNs);
    if (lastKeyframeRequestNs_ >= 0 && now - lastKeyframeRequestNs_ < kKeyframeThrottleNs) return;
    lastKeyframeRequestNs_ = now;
    ++stats_.keyframeRequestsDispatched;
    if (keyframeRequestHandler_)
        keyframeRequestHandler_((dataChannelVideo_ || videoDcOpen_) ? KeyframeChannel::DataChannel
                                                                    : KeyframeChannel::Rtcp);
}

void ReceiverEngine::emitFrame(const uint8_t* data, size_t size, int64_t mediaTicks, TimestampDomain domain,
                               bool fromDataChannel) {
    EncodedVideoFrame frame;
    frame.annexB.assign(data, data + size);
    frame.mediaTicks = mediaTicks;
    frame.timestampDomain = domain;
    frame.receivedMonotonicNs = currentArrivalNs_;
    frame.generation = generation_;
    frame.randomAccess = BoundedVideoQueue::configuredIdr(frame.annexB);
    if (frame.randomAccess) needKeyframe_ = false;
    if (fromDataChannel) ++stats_.dataChannelFrames;
    else ++stats_.rtpFrames;
    stats_.lastFrameArrivalNs = currentArrivalNs_;
    if (frameHandler_) frameHandler_(frame);
}

} // namespace km::engine
