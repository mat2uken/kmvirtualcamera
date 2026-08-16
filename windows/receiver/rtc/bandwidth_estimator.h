#pragma once

#include <cstdint>
#include <cstddef>
#include <mutex>
#include <vector>
#include <algorithm>

namespace km::rtc_net {

struct BandwidthEstimatorConfig {
    uint32_t minBitrateBps{500'000};    // 500 kbps
    uint32_t maxBitrateBps{5'000'000};  // 5.0 Mbps
    uint32_t startBitrateBps{3'500'000};// 3.5 Mbps
    int64_t windowDurationMs{1000};     // 1 second estimation window
    float lossThresholdDecrease{0.04f}; // 4% loss triggers backoff
};

class BandwidthEstimator {
public:
    explicit BandwidthEstimator(const BandwidthEstimatorConfig& config = BandwidthEstimatorConfig{});
    ~BandwidthEstimator() = default;

    void Reset();

    // Ingests incoming RTP packet metrics
    void OnRtpPacketReceived(size_t packetSize, uint16_t seqNum, int64_t arrivalTimeMs);

    // Explicit notification when keyframe is requested due to loss/corruption
    void OnLossEventDetected();

    // Called periodically (e.g. every 500ms-1000ms or on packet) to evaluate bandwidth & trigger REMB update
    // Returns true if outTargetBitrateBps has changed or needs a periodic REMB refresh
    bool EvaluateEstimation(int64_t currentTimeMs, uint32_t& outTargetBitrateBps);

    // Getters for telemetry / stats
    uint32_t GetCurrentEstimatedBitrate() const;
    uint32_t GetMeasuredThroughputBps() const;
    float GetCurrentLossRatio() const;

private:
    void ResetWindow(int64_t currentTimeMs);

    BandwidthEstimatorConfig config_;
    mutable std::mutex mutex_;

    uint32_t currentBitrateEstimateBps_{3'500'000};
    uint32_t measuredThroughputBps_{0};
    float currentLossRatio_{0.0f};

    int64_t windowStartTimeMs_{0};
    int64_t lastEvaluationTimeMs_{0};
    size_t windowBytesReceived_{0};
    uint32_t windowPacketsReceived_{0};
    uint32_t windowPacketsLost_{0};
    bool hasLastSeq_{false};
    uint16_t lastSeqNum_{0};
    int consecutiveCleanWindows_{0};
};

} // namespace km::rtc_net
