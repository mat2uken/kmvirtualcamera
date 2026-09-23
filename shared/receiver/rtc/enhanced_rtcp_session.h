#pragma once
#include <rtc/rtc.hpp>
#include <rtc/rtcpreceivingsession.hpp>
#include <array>
#include <memory>
#include <mutex>
#include "twcc_receiver.h"
namespace km::rtc_net {
class BandwidthEstimator;
class EnhancedRtcpReceivingSession : public rtc::RtcpReceivingSession {
public:
    explicit EnhancedRtcpReceivingSession(uint32_t clockRate = 90000);
    void incoming(rtc::message_vector& messages, const rtc::message_callback& send) override;
    bool requestBitrate(unsigned int bitrate, const rtc::message_callback& send) override;
    bool requestKeyframe(const rtc::message_callback& send) override;
    void RequestKeyframeDirect();
    void SetBandwidthEstimator(std::shared_ptr<BandwidthEstimator> estimator);
    void SetTransportCcExtensionId(uint8_t id);
    void FlushFeedback();
    void Stop();
private:
    void resetSource(uint32_t ssrc);
    void record(uint16_t sequence, uint32_t timestamp, int64_t nowUs);
    rtc::message_ptr report(int64_t nowMs);
    rtc::message_ptr remb(uint32_t bitrate) const;
    rtc::message_vector feedback(int64_t nowMs);
    std::mutex mutex_;
    bool stopped_ = false, started_ = false;
    rtc::message_callback send_;
    std::shared_ptr<BandwidthEstimator> estimator_;
    TwccReceiver twcc_;
    uint8_t twccId_ = 0, firSequence_ = 0;
    uint32_t ssrc_ = 0, lastTimestamp_ = 0, clockRate_ = 90000;
    uint64_t base_ = 0, highest_ = 0, received_ = 0, previousExpected_ = 0, previousReceived_ = 0;
    std::array<uint64_t, 2048> seen_;
    int64_t lastArrivalUs_ = 0;
    double jitter_ = 0;
    uint64_t srNtp_ = 0;
    int64_t srArrivalMs_ = -1, lastRr_ = 0, lastRemb_ = 0, lastTwcc_ = 0, lastPli_ = -1;
};
} // namespace km::rtc_net
