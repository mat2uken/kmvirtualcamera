#pragma once

#include <cstdint>
#include <cstddef>
#include <mutex>
#include <atomic>
#include <vector>
#include <algorithm>

namespace km::rtc_net {

struct BandwidthEstimatorConfig {
    uint32_t minBitrateBps{1'000'000};    // 1.0 Mbps
    uint32_t maxBitrateBps{2'800'000};    // 2.8 Mbps max (prevents Wi-Fi bufferbloat)
    uint32_t startBitrateBps{2'500'000};  // 2.5 Mbps (sweet spot for 720p 30fps)
    int64_t windowDurationMs{1000};       // 1 second estimation window
    float lossThresholdDecrease{0.04f};   // 4% loss triggers backoff
};

class BandwidthEstimator {
public:
    explicit BandwidthEstimator(const BandwidthEstimatorConfig& config = BandwidthEstimatorConfig{});
    ~BandwidthEstimator() = default;

    void Reset();

    // Lock-free ingestion of incoming RTP packet metrics
    void OnRtpPacketReceived(size_t packetSize, uint16_t seqNum, int64_t arrivalTimeMs);

    // Explicit notification when keyframe is requested due to loss/corruption
    void OnLossEventDetected();

    // Called periodically to evaluate bandwidth & trigger REMB update
    bool EvaluateEstimation(int64_t currentTimeMs, uint32_t& outTargetBitrateBps);

    // Lock-free getters for telemetry / stats
    uint32_t GetCurrentEstimatedBitrate() const;
    uint32_t GetMeasuredThroughputBps() const;
    float GetCurrentLossRatio() const;

private:

    BandwidthEstimatorConfig config_;
    mutable std::mutex evalMutex_;

    std::atomic<uint32_t> currentBitrateEstimateBps_{2'500'000};
    std::atomic<uint32_t> measuredThroughputBps_{0};
    std::atomic<float> currentLossRatio_{0.0f};

    std::atomic<int64_t> windowStartTimeMs_{0};
    std::atomic<int64_t> lastEvaluationTimeMs_{0};
    std::atomic<uint64_t> windowBytesReceived_{0};
    std::atomic<uint32_t> windowPacketsReceived_{0};
    std::atomic<uint32_t> windowPacketsLost_{0};
    std::atomic<bool> hasLastSeq_{false};
    std::atomic<uint16_t> lastSeqNum_{0};
    std::atomic<int> consecutiveCleanWindows_{0};
};

} // namespace km::rtc_net
