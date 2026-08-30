#pragma once

#include <rtc/rtc.hpp>
#include <rtc/rtcpreceivingsession.hpp>
#include <rtc/rtp.hpp>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <chrono>
#include "twcc_receiver.h"

namespace km::rtc_net {

class BandwidthEstimator;  // forward declaration

/// Enhanced RTCP Receiving Session that provides:
/// 1. Accurate Receiver Reports with real loss/jitter/sequence data
/// 2. Periodic REMB sending based on BandwidthEstimator
/// 3. TWCC feedback generation for browser GCC
/// 4. Cached send callback for timer-based periodic feedback
class EnhancedRtcpReceivingSession : public rtc::RtcpReceivingSession {
public:
    EnhancedRtcpReceivingSession();
    ~EnhancedRtcpReceivingSession() override = default;

    void incoming(rtc::message_vector &messages, const rtc::message_callback &send) override;
    bool requestBitrate(unsigned int bitrate, const rtc::message_callback &send) override;
    bool requestKeyframe(const rtc::message_callback &send) override;

    /// Connect the bandwidth estimator for REMB values
    void SetBandwidthEstimator(BandwidthEstimator* estimator);

    /// Set transport-cc extension ID from SDP negotiation. 0 = TWCC disabled.
    void SetTransportCcExtensionId(uint8_t id);

    /// Called from external timer thread to flush periodic feedback (TWCC, RR, REMB)
    void FlushFeedback();

private:
    void ProcessRtpStats(const uint8_t* data, size_t size, int64_t nowUs);
    void SendEnhancedRR(const rtc::message_callback &send, int64_t nowMs);
    void SendTwccFeedback(const rtc::message_callback &send);
    void CheckAndSendPeriodicFeedback(const rtc::message_callback &send, int64_t nowMs);

    // Send callback cache for timer-based sending
    std::mutex sendMutex_;
    rtc::message_callback cachedSend_;

    // RTP statistics for Receiver Report
    std::mutex statsMutex_;
    uint32_t totalPacketsReceived_ = 0;
    uint16_t highestSeqReceived_ = 0;
    uint16_t seqCycles_ = 0;  // number of seq wrap-arounds
    uint16_t baseSeq_ = 0;
    bool hasFirstPacket_ = false;

    // Fraction lost calculation (interval-based)
    uint32_t lastIntervalPacketsReceived_ = 0;
    uint32_t lastIntervalExpectedPackets_ = 0;
    uint16_t lastIntervalHighestSeq_ = 0;

    // Jitter calculation (RFC 3550 Section 6.4.1)
    uint32_t lastRtpTimestamp_ = 0;
    int64_t lastArrivalTimeUs_ = 0;
    double interarrivalJitter_ = 0.0;  // in RTP timestamp units (90kHz)
    bool hasLastTimestamp_ = false;

    // SR timing for DLSR calculation
    uint64_t lastSrNtp_ = 0;
    int64_t lastSrReceivedMs_ = 0;
    bool hasLastSr_ = false;

    // TWCC
    TwccReceiver twccReceiver_;
    std::atomic<uint8_t> transportCcExtId_{0};

    // Bandwidth estimator
    BandwidthEstimator* bandwidthEstimator_ = nullptr;

    // Periodic send timing
    std::atomic<int64_t> lastRrSentMs_{0};
    std::atomic<int64_t> lastRembSentMs_{0};
    std::atomic<int64_t> lastTwccSentMs_{0};

    static constexpr int64_t kRrIntervalMs = 500;
    static constexpr int64_t kRembIntervalMs = 1000;
    static constexpr int64_t kTwccIntervalMs = 25; // 25ms (40Hz) ultra-low-latency feedback
};

} // namespace km::rtc_net
