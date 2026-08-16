#include "bandwidth_estimator.h"
#include <iostream>

namespace km::rtc_net {

BandwidthEstimator::BandwidthEstimator(const BandwidthEstimatorConfig& config)
    : config_(config), currentBitrateEstimateBps_(config.startBitrateBps) {
}

void BandwidthEstimator::Reset() {
    std::lock_guard<std::mutex> lock(evalMutex_);
    currentBitrateEstimateBps_.store(config_.startBitrateBps, std::memory_order_relaxed);
    measuredThroughputBps_.store(0, std::memory_order_relaxed);
    currentLossRatio_.store(0.0f, std::memory_order_relaxed);
    windowStartTimeMs_.store(0, std::memory_order_relaxed);
    lastEvaluationTimeMs_.store(0, std::memory_order_relaxed);
    windowBytesReceived_.store(0, std::memory_order_relaxed);
    windowPacketsReceived_.store(0, std::memory_order_relaxed);
    windowPacketsLost_.store(0, std::memory_order_relaxed);
    hasLastSeq_.store(false, std::memory_order_relaxed);
    lastSeqNum_.store(0, std::memory_order_relaxed);
    consecutiveCleanWindows_.store(0, std::memory_order_relaxed);
}

void BandwidthEstimator::OnRtpPacketReceived(size_t packetSize, uint16_t seqNum, int64_t arrivalTimeMs) {
    int64_t expectedStart = 0;
    windowStartTimeMs_.compare_exchange_strong(expectedStart, arrivalTimeMs, std::memory_order_relaxed);

    if (hasLastSeq_.load(std::memory_order_relaxed)) {
        uint16_t prev = lastSeqNum_.load(std::memory_order_relaxed);
        uint16_t diff = static_cast<uint16_t>(seqNum - prev);
        if (diff > 1 && diff < 3000) {
            windowPacketsLost_.fetch_add(diff - 1, std::memory_order_relaxed);
        }
    } else {
        hasLastSeq_.store(true, std::memory_order_relaxed);
    }
    lastSeqNum_.store(seqNum, std::memory_order_relaxed);

    windowPacketsReceived_.fetch_add(1, std::memory_order_relaxed);
    windowBytesReceived_.fetch_add(packetSize, std::memory_order_relaxed);
}

void BandwidthEstimator::OnLossEventDetected() {
    uint32_t current = currentBitrateEstimateBps_.load(std::memory_order_relaxed);
    uint32_t decreased = (std::max)(
        config_.minBitrateBps,
        static_cast<uint32_t>(current * 0.85f)
    );
    currentBitrateEstimateBps_.store(decreased, std::memory_order_relaxed);
    consecutiveCleanWindows_.store(0, std::memory_order_relaxed);
}

bool BandwidthEstimator::EvaluateEstimation(int64_t currentTimeMs, uint32_t& outTargetBitrateBps) {
    std::lock_guard<std::mutex> lock(evalMutex_);
    int64_t winStart = windowStartTimeMs_.load(std::memory_order_relaxed);
    if (winStart == 0) {
        windowStartTimeMs_.store(currentTimeMs, std::memory_order_relaxed);
        lastEvaluationTimeMs_.store(currentTimeMs, std::memory_order_relaxed);
        outTargetBitrateBps = currentBitrateEstimateBps_.load(std::memory_order_relaxed);
        return false;
    }

    int64_t elapsedWindowMs = currentTimeMs - winStart;
    if (elapsedWindowMs >= config_.windowDurationMs) {
        uint64_t bytes = windowBytesReceived_.exchange(0, std::memory_order_relaxed);
        uint32_t pktsRecv = windowPacketsReceived_.exchange(0, std::memory_order_relaxed);
        uint32_t pktsLost = windowPacketsLost_.exchange(0, std::memory_order_relaxed);

        uint32_t throughput = static_cast<uint32_t>(
            (bytes * 8ULL * 1000ULL) / static_cast<uint64_t>(elapsedWindowMs)
        );
        measuredThroughputBps_.store(throughput, std::memory_order_relaxed);

        uint32_t totalPackets = pktsRecv + pktsLost;
        float lossRatio = (totalPackets > 0)
            ? (static_cast<float>(pktsLost) / static_cast<float>(totalPackets))
            : 0.0f;
        currentLossRatio_.store(lossRatio, std::memory_order_relaxed);

        uint32_t curEst = currentBitrateEstimateBps_.load(std::memory_order_relaxed);
        if (lossRatio >= config_.lossThresholdDecrease) {
            // Congestion detected: Multiplicative decrease
            curEst = (std::max)(
                config_.minBitrateBps,
                static_cast<uint32_t>(curEst * 0.80f)
            );
            consecutiveCleanWindows_.store(0, std::memory_order_relaxed);
        } else if (lossRatio == 0.0f) {
            int cleanCount = consecutiveCleanWindows_.fetch_add(1, std::memory_order_relaxed) + 1;
            if (cleanCount >= 2) {
                // Stable network: Additive increase
                curEst = (std::min)(
                    config_.maxBitrateBps,
                    curEst + 100'000
                );
            }
        }
        currentBitrateEstimateBps_.store(curEst, std::memory_order_relaxed);

        windowStartTimeMs_.store(currentTimeMs, std::memory_order_relaxed);
        lastEvaluationTimeMs_.store(currentTimeMs, std::memory_order_relaxed);
        outTargetBitrateBps = curEst;
        return true;
    }

    // Periodic REMB heartbeat (every 1000ms)
    int64_t lastEval = lastEvaluationTimeMs_.load(std::memory_order_relaxed);
    if (currentTimeMs - lastEval >= 1000) {
        lastEvaluationTimeMs_.store(currentTimeMs, std::memory_order_relaxed);
        outTargetBitrateBps = currentBitrateEstimateBps_.load(std::memory_order_relaxed);
        return true;
    }

    return false;
}

uint32_t BandwidthEstimator::GetCurrentEstimatedBitrate() const {
    return currentBitrateEstimateBps_.load(std::memory_order_relaxed);
}

uint32_t BandwidthEstimator::GetMeasuredThroughputBps() const {
    return measuredThroughputBps_.load(std::memory_order_relaxed);
}

float BandwidthEstimator::GetCurrentLossRatio() const {
    return currentLossRatio_.load(std::memory_order_relaxed);
}

} // namespace km::rtc_net
