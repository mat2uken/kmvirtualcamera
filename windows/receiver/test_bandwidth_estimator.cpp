#include "rtc/bandwidth_estimator.h"
#include <iostream>
#include <cassert>

using namespace km::rtc_net;

void TestInitialState() {
    std::cout << "[TEST] 1. Initial State..." << std::endl;
    BandwidthEstimator bwe;
    assert(bwe.GetCurrentEstimatedBitrate() == 2'500'000);
    assert(bwe.GetMeasuredThroughputBps() == 0);
    assert(bwe.GetCurrentLossRatio() == 0.0f);
    std::cout << "  -> PASS" << std::endl;
}

void TestCleanTrafficAdditiveIncrease() {
    std::cout << "[TEST] 2. Clean Traffic Additive Increase..." << std::endl;
    BandwidthEstimatorConfig config;
    config.startBitrateBps = 2'000'000;
    config.maxBitrateBps = 4'000'000;
    config.windowDurationMs = 1000;
    BandwidthEstimator bwe(config);

    // Simulate 3 seconds of clean RTP packet stream (100 packets/sec @ 1200 bytes)
    uint16_t seq = 100;
    int64_t timeMs = 1000;

    for (int sec = 1; sec <= 3; ++sec) {
        for (int p = 0; p < 100; ++p) {
            bwe.OnRtpPacketReceived(1200, seq++, timeMs);
            timeMs += 10;
        }

        uint32_t target = 0;
        bool updated = bwe.EvaluateEstimation(timeMs, target);
        assert(updated);
        std::cout << "  Sec " << sec << ": Target Bitrate = " << target / 1000 << " kbps, Throughput = "
                  << bwe.GetMeasuredThroughputBps() / 1000 << " kbps, Loss = " << bwe.GetCurrentLossRatio() << std::endl;
    }

    assert(bwe.GetCurrentEstimatedBitrate() > 2'000'000);
    assert(bwe.GetCurrentLossRatio() == 0.0f);
    std::cout << "  -> PASS" << std::endl;
}

void TestPacketLossMultiplicativeDecrease() {
    std::cout << "[TEST] 3. Packet Loss Multiplicative Decrease..." << std::endl;
    BandwidthEstimatorConfig config;
    config.startBitrateBps = 3'500'000;
    config.minBitrateBps = 800'000;
    config.windowDurationMs = 1000;
    BandwidthEstimator bwe(config);

    uint16_t seq = 500;
    int64_t timeMs = 1000;

    // Simulate 100 packets with 10 dropped packets (10% loss)
    for (int p = 0; p < 100; ++p) {
        if (p == 40) {
            seq += 10; // 10 packets lost
        }
        bwe.OnRtpPacketReceived(1200, seq++, timeMs);
        timeMs += 10;
    }

    uint32_t target = 0;
    bool updated = bwe.EvaluateEstimation(timeMs, target);
    assert(updated);
    std::cout << "  After Loss: Target Bitrate = " << target / 1000 << " kbps, Loss Ratio = " << bwe.GetCurrentLossRatio() << std::endl;

    assert(bwe.GetCurrentLossRatio() > 0.05f);
    assert(target < 3'500'000);
    assert(target >= 800'000);
    assert(target == 2'800'000); // 3,500,000 * 0.80 = 2,800,000
    std::cout << "  -> PASS" << std::endl;
}

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << "  BANDWIDTH ESTIMATOR & ABR UNIT TEST SUITE                 " << std::endl;
    std::cout << "============================================================" << std::endl;

    TestInitialState();
    TestCleanTrafficAdditiveIncrease();
    TestPacketLossMultiplicativeDecrease();

    std::cout << "============================================================" << std::endl;
    std::cout << "  ALL BANDWIDTH ESTIMATOR TESTS PASSED (100%)              " << std::endl;
    std::cout << "============================================================" << std::endl;
    return 0;
}
