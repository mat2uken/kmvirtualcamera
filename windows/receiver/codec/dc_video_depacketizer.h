#pragma once

#include <cstdint>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>
#include <string>
#include "../../common/data_channel_protocol.h"

namespace km::codec {

struct IncompleteDcFrame {
    uint16_t frameSeq{0};
    uint32_t timestampUs{0};
    uint8_t totalChunks{0};
    uint8_t receivedChunks{0};
    bool isKeyframe{false};
    bool isComplete{false};
    int64_t firstChunkArrivalUs{0};
    std::vector<std::vector<uint8_t>> chunks;
};

class DcVideoDepacketizer {
public:
    using FrameCallback = std::function<void(const uint8_t* data, size_t size, int64_t timestampUs)>;
    using ControlSendCallback = std::function<void(const std::string& jsonMsg)>;

    DcVideoDepacketizer();
    ~DcVideoDepacketizer() = default;

    void SetCallback(FrameCallback cb) { frameCallback_ = std::move(cb); }
    void SetControlSendCallback(ControlSendCallback cb) { controlSendCallback_ = std::move(cb); }

    void Reset();

    // Process binary packet received from DataChannel
    void ProcessDataChannelPacket(const uint8_t* data, size_t size);

    // Periodic timer tick (every 20ms) for timeout flush and BWE
    void OnTimerTick();

    uint32_t GetCurrentEstimatedBitrate() const { return currentEstimatedBps_.load(std::memory_order_relaxed); }
    uint64_t GetTotalFramesAssembled() const { return totalFramesAssembled_.load(std::memory_order_relaxed); }

private:
    void DrainCompletedFrames(int64_t nowUs);
    void EmitFrame(IncompleteDcFrame& frame, int64_t nowUs);
    void RequestKeyframe();
    void EvaluateDelayGradientBwe(uint32_t senderTsUs, int64_t arrivalTsUs, size_t frameSize);

    FrameCallback frameCallback_;
    ControlSendCallback controlSendCallback_;

    std::mutex mutex_;
    std::map<uint16_t, IncompleteDcFrame> pendingFramesMap_;
    std::vector<uint8_t> assemblyBuffer_;
    std::vector<uint8_t> cachedSpsPps_;

    uint16_t expectedSeq_{0};
    bool hasInitializedSeq_{false};
    bool waitingForKeyframe_{true};

    // Congestion Control / Delay Gradient Filter
    int64_t lastArrivalUs_{0};
    uint32_t lastSenderTsUs_{0};
    double smoothedDelayGradientUs_{0.0};
    std::atomic<uint32_t> currentEstimatedBps_{3000000};
    int64_t lastBitrateUpdateMs_{0};
    int64_t lastPliSentMs_{0};

    std::atomic<uint64_t> totalFramesAssembled_{0};
    std::atomic<uint64_t> totalPacketsReceived_{0};
};

} // namespace km::codec
