#include "enhanced_rtcp_session.h"
#include "bandwidth_estimator.h"
#include "../../km/rtp_wire.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
namespace km::rtc_net {
namespace {
int64_t nowUs() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
void put32(uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v); }
rtc::message_ptr packet(size_t size, uint8_t type, uint8_t count) {
    auto m = rtc::make_message(size, rtc::Message::Control);
    auto* p = reinterpret_cast<uint8_t*>(m->data()); std::memset(p, 0, size);
    p[0] = 128 | count; p[1] = type; p[2] = uint8_t((size / 4 - 1) >> 8); p[3] = uint8_t(size / 4 - 1);
    put32(p + 4, 1); return m;
}
void deliver(const rtc::message_callback& send, rtc::message_vector messages) {
    if (send) for (auto& message : messages) send(message);
}
}
EnhancedRtcpReceivingSession::EnhancedRtcpReceivingSession(uint32_t clockRate) : clockRate_(clockRate) {
    seen_.fill(UINT64_MAX); lastRr_ = lastRemb_ = lastTwcc_ = nowUs() / 1000;
}
void EnhancedRtcpReceivingSession::SetBandwidthEstimator(std::shared_ptr<BandwidthEstimator> value) {
    std::lock_guard lock(mutex_); estimator_ = std::move(value);
}
void EnhancedRtcpReceivingSession::SetTransportCcExtensionId(uint8_t id) { std::lock_guard lock(mutex_); twccId_ = id; }
void EnhancedRtcpReceivingSession::Stop() { std::lock_guard lock(mutex_); stopped_ = true; send_ = {}; estimator_.reset(); }
void EnhancedRtcpReceivingSession::resetSource(uint32_t ssrc) {
    ssrc_ = ssrc; started_ = false; seen_.fill(UINT64_MAX); twcc_.Reset();
    base_ = highest_ = received_ = previousExpected_ = previousReceived_ = 0;
    srNtp_ = 0; srArrivalMs_ = -1; jitter_ = 0;
}
void EnhancedRtcpReceivingSession::record(uint16_t seq, uint32_t ts, int64_t arrival) {
    uint64_t extended = seq;
    if (!started_) { base_ = highest_ = seq; started_ = true; lastTimestamp_ = ts; lastArrivalUs_ = arrival; }
    else {
        const int delta = wire::sequenceDelta(seq, uint16_t(highest_));
        if (delta < 0 && uint64_t(-delta) > highest_) return;
        extended = delta >= 0 ? highest_ + unsigned(delta) : highest_ - unsigned(-delta);
        if (extended < base_ || (highest_ > extended && highest_ - extended >= seen_.size())) return;
        if (seen_[extended % seen_.size()] == extended) return;
        highest_ = std::max(highest_, extended);
        const uint32_t raw = ts - lastTimestamp_;
        const int64_t timestampDelta = raw <= INT32_MAX ? int64_t(raw) : int64_t(raw) - (int64_t(1) << 32);
        const double transit = double(arrival - lastArrivalUs_) * double(clockRate_) / 1000000.0 - double(timestampDelta);
        jitter_ += (std::abs(transit) - jitter_) / 16.0;
    }
    seen_[extended % seen_.size()] = extended; ++received_;
    lastTimestamp_ = ts; lastArrivalUs_ = arrival;
}
rtc::message_ptr EnhancedRtcpReceivingSession::report(int64_t now) {
    auto m = packet(32, 201, 1); auto* p = reinterpret_cast<uint8_t*>(m->data());
    const uint64_t expected = highest_ - base_ + 1;
    const uint64_t intervalExpected = expected - previousExpected_, intervalReceived = received_ - previousReceived_;
    const uint64_t lost = expected > received_ ? expected - received_ : 0;
    const uint64_t intervalLost = intervalExpected > intervalReceived ? intervalExpected - intervalReceived : 0;
    previousExpected_ = expected; previousReceived_ = received_;
    put32(p + 8, ssrc_);
    p[12] = intervalExpected ? uint8_t(std::min<uint64_t>(255, intervalLost * 256 / intervalExpected)) : 0;
    const uint32_t clamped = uint32_t(std::min<uint64_t>(0x7fffff, lost));
    p[13] = uint8_t(clamped >> 16); p[14] = uint8_t(clamped >> 8); p[15] = uint8_t(clamped);
    put32(p + 16, uint32_t(highest_));
    put32(p + 20, uint32_t(std::clamp(jitter_, 0.0, double(UINT32_MAX))));
    put32(p + 24, uint32_t(srNtp_ >> 16));
    const uint64_t delay = srArrivalMs_ >= 0 && now >= srArrivalMs_ ? uint64_t(now - srArrivalMs_) * 65536 / 1000 : 0;
    put32(p + 28, uint32_t(std::min<uint64_t>(UINT32_MAX, delay))); return m;
}
rtc::message_ptr EnhancedRtcpReceivingSession::remb(uint32_t bitrate) const {
    auto m = packet(24, 206, 15); auto* p = reinterpret_cast<uint8_t*>(m->data());
    std::memcpy(p + 12, "REMB", 4); p[16] = 1;
    uint8_t exponent = 0; while (bitrate > 0x3ffff) { bitrate >>= 1; ++exponent; }
    p[17] = uint8_t((exponent << 2) | (bitrate >> 16)); p[18] = uint8_t(bitrate >> 8); p[19] = uint8_t(bitrate);
    put32(p + 20, ssrc_); return m;
}
rtc::message_vector EnhancedRtcpReceivingSession::feedback(int64_t now) {
    rtc::message_vector out;
    if (!ssrc_ || stopped_) return out;
    if (started_ && now - lastRr_ >= 500) { out.push_back(report(now)); lastRr_ = now; }
    if (estimator_ && now - lastRemb_ >= 1000) { out.push_back(remb(estimator_->GetCurrentEstimatedBitrate())); lastRemb_ = now; }
    if (twccId_ && (now - lastTwcc_ >= 25 || twcc_.GetPendingPacketCount() >= 16)) {
        auto bytes = twcc_.BuildFeedbackPacket(1, ssrc_); lastTwcc_ = now;
        if (!bytes.empty()) {
            auto m = rtc::make_message(bytes.size(), rtc::Message::Control);
            std::memcpy(m->data(), bytes.data(), bytes.size()); out.push_back(std::move(m));
        }
    }
    return out;
}
void EnhancedRtcpReceivingSession::incoming(rtc::message_vector& messages, const rtc::message_callback& send) {
    rtc::message_vector output, feedbackMessages;
    {
        std::lock_guard lock(mutex_);
        if (stopped_) { messages.clear(); return; }
        send_ = send; const int64_t now = nowUs();
        for (auto& message : messages) {
            wire::Bytes bytes{reinterpret_cast<const uint8_t*>(message->data()), message->size()};
            if (message->type == rtc::Message::Binary) {
                const auto rtp = wire::parseRtp(bytes); if (!rtp) continue;
                if (ssrc_ != rtp->ssrc) resetSource(rtp->ssrc);
                record(rtp->sequence, rtp->timestamp, now);
                uint16_t transportSequence = 0;
                if (twccId_ && TwccReceiver::ParseTransportSequence(bytes.data(), bytes.size(), twccId_, transportSequence))
                    twcc_.OnPacket(transportSequence, now, uint16_t(bytes.size()));
                output.push_back(std::move(message));
            } else if (message->type == rtc::Message::Control) {
                std::vector<wire::RtcpPacket> compound;
                if (!wire::parseRtcp(bytes, compound)) continue;
                for (const auto& part : compound) {
                    // RR.senderSSRC describes the report sender, NOT our media source.
                    if (part.type == 200) {
                        const auto source = wire::be32(part.body.data() + 4);
                        if (ssrc_ && ssrc_ != source) continue;
                        ssrc_ = source; srNtp_ = wire::be64(part.body.data() + 8); srArrivalMs_ = now / 1000;
                    }
                }
            }
        }
        feedbackMessages = feedback(now / 1000);
    }
    messages.swap(output); deliver(send, std::move(feedbackMessages));
}
void EnhancedRtcpReceivingSession::FlushFeedback() {
    rtc::message_callback send; rtc::message_vector messages;
    { std::lock_guard lock(mutex_); if (stopped_) return; send = send_; messages = feedback(nowUs() / 1000); }
    deliver(send, std::move(messages));
}
bool EnhancedRtcpReceivingSession::requestBitrate(unsigned int bitrate, const rtc::message_callback& send) {
    rtc::message_vector messages;
    { std::lock_guard lock(mutex_); if (stopped_) return false; send_ = send; if (ssrc_) messages.push_back(remb(bitrate)); }
    deliver(send, std::move(messages)); return true;
}
bool EnhancedRtcpReceivingSession::requestKeyframe(const rtc::message_callback& send) {
    rtc::message_vector messages;
    {
        std::lock_guard lock(mutex_); if (stopped_) return false; send_ = send;
        const int64_t now = nowUs() / 1000;
        if (ssrc_ && (lastPli_ < 0 || now - lastPli_ >= 50)) {
            lastPli_ = now;
            auto pli = packet(12, 206, 1); put32(reinterpret_cast<uint8_t*>(pli->data()) + 8, ssrc_); messages.push_back(pli);
            auto fir = packet(20, 206, 4); auto* p = reinterpret_cast<uint8_t*>(fir->data());
            put32(p + 12, ssrc_); p[16] = firSequence_++; messages.push_back(fir);
        }
    }
    deliver(send, std::move(messages)); return true;
}
void EnhancedRtcpReceivingSession::RequestKeyframeDirect() {
    rtc::message_callback send;
    { std::lock_guard lock(mutex_); if (stopped_) return; send = send_; }
    if (send) requestKeyframe(send);
}
} // namespace km::rtc_net
