#include "bandwidth_estimator.h"
#include <iostream>

namespace km::rtc_net {

BandwidthEstimator::BandwidthEstimator(const BandwidthEstimatorConfig& config)
    : config_(config), currentBitrateEstimateBps_(config.startBitrateBps) {
}

void BandwidthEstimator::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    currentBitrateEstimateBps_ = config_.startBitrateBps;
    measuredThroughputBps_ = 0;
    currentLossRatio_ = 0.0f;
    windowStartTimeMs_ = 0;
    lastEvaluationTimeMs_ = 0;
    windowBytesReceived_ = 0;
    windowPacketsReceived_ = 0;
    windowPacketsLost_ = 0;
    hasLastSeq_ = false;
    lastSeqNum_ = 0;
    consecutiveCleanWindows_ = 0;
}

void BandwidthEstimator::OnRtpPacketReceived(size_t packetSize, uint16_t seqNum, int64_t arrivalTimeMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (windowStartTimeMs_ == 0) {
        windowStartTimeMs_ = arrivalTimeMs;
        lastEvaluationTimeMs_ = arrivalTimeMs;
    }

    if (hasLastSeq_) {
        uint16_t diff = static_cast<uint16_t>(seqNum - lastSeqNum_);
        if (diff > 1 && diff < 3000) {
            windowPacketsLost_ += (diff - 1);
        }
    }
    hasLastSeq_ = true;
    lastSeqNum_ = seqNum;

    windowPacketsReceived_++;
    windowBytesReceived_ += packetSize;
}

void BandwidthEstimator::OnLossEventDetected() {
    std::lock_guard<std::mutex> lock(mutex_);
    currentBitrateEstimateBps_ = (std::max)(
        config_.minBitrateBps,
        static_cast<uint32_t>(currentBitrateEstimateBps_ * 0.85f)
    );
    consecutiveCleanWindows_ = 0;
}

bool BandwidthEstimator::EvaluateEstimation(int64_t currentTimeMs, uint32_t& outTargetBitrateBps) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (windowStartTimeMs_ == 0) {
        windowStartTimeMs_ = currentTimeMs;
        lastEvaluationTimeMs_ = currentTimeMs;
        outTargetBitrateBps = currentBitrateEstimateBps_;
        return false;
    }

    int64_t elapsedWindowMs = currentTimeMs - windowStartTimeMs_;
    if (elapsedWindowMs >= config_.windowDurationMs) {
        measuredThroughputBps_ = static_cast<uint32_t>(
            (windowBytesReceived_ * 8ULL * 1000ULL) / static_cast<uint64_t>(elapsedWindowMs)
        );

        uint32_t totalPackets = windowPacketsReceived_ + windowPacketsLost_;
        currentLossRatio_ = (totalPackets > 0)
            ? (static_cast<float>(windowPacketsLost_) / static_cast<float>(totalPackets))
            : 0.0f;

        if (currentLossRatio_ >= config_.lossThresholdDecrease) {
            // Congestion detected: Multiplicative decrease
            currentBitrateEstimateBps_ = (std::max)(
                config_.minBitrateBps,
                static_cast<uint32_t>(currentBitrateEstimateBps_ * 0.80f)
            );
            consecutiveCleanWindows_ = 0;
        } else if (currentLossRatio_ == 0.0f) {
            consecutiveCleanWindows_++;
            if (consecutiveCleanWindows_ >= 2) {
                // Stable network: Additive increase
                currentBitrateEstimateBps_ = (std::min)(
                    config_.maxBitrateBps,
                    currentBitrateEstimateBps_ + 250'000
                );
            }
        }

        ResetWindow(currentTimeMs);
        lastEvaluationTimeMs_ = currentTimeMs;
        outTargetBitrateBps = currentBitrateEstimateBps_;
        return true;
    }

    // Periodic REMB heartbeat (every 1000ms)
    if (currentTimeMs - lastEvaluationTimeMs_ >= 1000) {
        lastEvaluationTimeMs_ = currentTimeMs;
        outTargetBitrateBps = currentBitrateEstimateBps_;
        return true;
    }

    return false;
}

void BandwidthEstimator::ResetWindow(int64_t currentTimeMs) {
    windowStartTimeMs_ = currentTimeMs;
    windowBytesReceived_ = 0;
    windowPacketsReceived_ = 0;
    windowPacketsLost_ = 0;
}

uint32_t BandwidthEstimator::GetCurrentEstimatedBitrate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentBitrateEstimateBps_;
}

uint32_t BandwidthEstimator::GetMeasuredThroughputBps() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return measuredThroughputBps_;
}

float BandwidthEstimator::GetCurrentLossRatio() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentLossRatio_;
}

} // namespace km::rtc_net
