#include "dc_video_depacketizer.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>

namespace km::codec {

static const uint8_t kStartSequence[4] = { 0x00, 0x00, 0x00, 0x01 };

DcVideoDepacketizer::DcVideoDepacketizer() {
    assemblyBuffer_.reserve(256 * 1024);
    pendingFrames_.reserve(kMaxPendingFrames);
}

void DcVideoDepacketizer::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingFrames_.clear();
    assemblyBuffer_.clear();
    cachedSpsPps_.clear();
    hasReceivedFirstFrame_ = false;
    waitingForKeyframe_ = true;
    lastArrivalUs_ = 0;
    lastSenderTsUs_ = 0;
    smoothedDelayGradientUs_ = 0.0;
    currentEstimatedBps_.store(3000000, std::memory_order_relaxed);
    totalFramesAssembled_.store(0, std::memory_order_relaxed);
    totalPacketsReceived_.store(0, std::memory_order_relaxed);
}

void DcVideoDepacketizer::ProcessDataChannelPacket(const uint8_t* data, size_t size) {
    if (!data || size < sizeof(dc_protocol::DcPacketHeader)) return;

    const auto* header = reinterpret_cast<const dc_protocol::DcPacketHeader*>(data);
    if (header->magic != dc_protocol::kDcMagic) return;
    if (header->payloadType != dc_protocol::kPayloadH264) return;
    if (header->totalChunks == 0 || header->chunkIndex >= header->totalChunks) return;

    size_t payloadSize = size - sizeof(dc_protocol::DcPacketHeader);
    const uint8_t* payload = data + sizeof(dc_protocol::DcPacketHeader);

    auto nowTime = std::chrono::steady_clock::now();
    int64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(nowTime.time_since_epoch()).count();
    totalPacketsReceived_.fetch_add(1, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(mutex_);

    // 1. Check sequence number & detect gaps
    uint16_t seq = header->frameSeq;
    if (hasReceivedFirstFrame_) {
        int16_t diff = static_cast<int16_t>(seq - highestSeqReceived_);
        if (diff > 1) {
            // Packet loss / frame gap detected
            if (waitingForKeyframe_ || (header->flags & dc_protocol::kFlagKeyframe)) {
                RequestKeyframe();
            }
        }
        if (diff > 0) {
            highestSeqReceived_ = seq;
        }
    } else {
        hasReceivedFirstFrame_ = true;
        highestSeqReceived_ = seq;
    }

    // 2. Find or create IncompleteDcFrame in pending queue
    IncompleteDcFrame* targetFrame = nullptr;
    for (auto& pf : pendingFrames_) {
        if (pf.frameSeq == seq) {
            targetFrame = &pf;
            break;
        }
    }

    if (!targetFrame) {
        // Clean oldest frame if queue is full
        if (pendingFrames_.size() >= kMaxPendingFrames) {
            pendingFrames_.erase(pendingFrames_.begin());
        }

        pendingFrames_.emplace_back();
        targetFrame = &pendingFrames_.back();
        targetFrame->frameSeq = seq;
        targetFrame->timestampUs = header->timestampUs;
        targetFrame->totalChunks = header->totalChunks;
        targetFrame->receivedChunks = 0;
        targetFrame->isKeyframe = (header->flags & dc_protocol::kFlagKeyframe) != 0;
        targetFrame->firstChunkArrivalUs = nowUs;
        targetFrame->chunks.resize(header->totalChunks);
    }

    // 3. Store chunk data if not already received
    if (targetFrame->chunks[header->chunkIndex].empty()) {
        targetFrame->chunks[header->chunkIndex].assign(payload, payload + payloadSize);
        targetFrame->receivedChunks++;
    }

    // 4. If all chunks received, assemble and emit immediately (0ms delay)
    if (targetFrame->receivedChunks == targetFrame->totalChunks) {
        AssembleAndEmit(*targetFrame);

        // Remove from pending list
        for (auto it = pendingFrames_.begin(); it != pendingFrames_.end(); ++it) {
            if (it->frameSeq == seq) {
                pendingFrames_.erase(it);
                break;
            }
        }
    }

    // 5. Clean up expired frames older than 100ms
    pendingFrames_.erase(
        std::remove_if(pendingFrames_.begin(), pendingFrames_.end(), [nowUs](const IncompleteDcFrame& f) {
            return (nowUs - f.firstChunkArrivalUs) > 100000;
        }),
        pendingFrames_.end()
    );
}

void DcVideoDepacketizer::AssembleAndEmit(IncompleteDcFrame& frame) {
    assemblyBuffer_.clear();

    for (const auto& chunk : frame.chunks) {
        assemblyBuffer_.insert(assemblyBuffer_.end(), chunk.begin(), chunk.end());
    }

    if (assemblyBuffer_.empty()) return;

    // Cache SPS/PPS if keyframe
    if (frame.isKeyframe) {
        bool hasSps = (assemblyBuffer_.size() > 4 && (assemblyBuffer_[4] & 0x1F) == 7);
        if (hasSps) {
            cachedSpsPps_.assign(assemblyBuffer_.begin(), assemblyBuffer_.end());
        }
        waitingForKeyframe_ = false;
    }

    if (!waitingForKeyframe_ && frameCallback_) {
        frameCallback_(assemblyBuffer_.data(), assemblyBuffer_.size(), static_cast<int64_t>(frame.timestampUs));
        totalFramesAssembled_.fetch_add(1, std::memory_order_relaxed);
    }

    auto nowTime = std::chrono::steady_clock::now();
    int64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(nowTime.time_since_epoch()).count();
    EvaluateDelayGradientBwe(frame.timestampUs, nowUs, assemblyBuffer_.size());
}

void DcVideoDepacketizer::EvaluateDelayGradientBwe(uint32_t senderTsUs, int64_t arrivalTsUs, size_t frameSize) {
    if (lastArrivalUs_ == 0 || lastSenderTsUs_ == 0) {
        lastArrivalUs_ = arrivalTsUs;
        lastSenderTsUs_ = senderTsUs;
        return;
    }

    int64_t deltaArrivalUs = arrivalTsUs - lastArrivalUs_;
    int64_t deltaSenderUs = static_cast<int64_t>(senderTsUs - lastSenderTsUs_);

    lastArrivalUs_ = arrivalTsUs;
    lastSenderTsUs_ = senderTsUs;

    // Reject outliers (e.g. clock jumps / sleep)
    if (deltaArrivalUs <= 0 || deltaArrivalUs > 200000 || deltaSenderUs <= 0 || deltaSenderUs > 200000) {
        return;
    }

    // Delay gradient: Delta = (A_i - A_{i-1}) - (S_i - S_{i-1})
    int64_t delayGradientUs = deltaArrivalUs - deltaSenderUs;

    // Smooth delay gradient with EMA (Exponential Moving Average)
    smoothedDelayGradientUs_ = 0.9 * smoothedDelayGradientUs_ + 0.1 * static_cast<double>(delayGradientUs);

    auto nowTime = std::chrono::steady_clock::now();
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(nowTime.time_since_epoch()).count();

    // Congestion Control Overuse / Underuse Detection (AIMD)
    uint32_t currentBps = currentEstimatedBps_.load(std::memory_order_relaxed);
    uint32_t targetBps = currentBps;

    if (smoothedDelayGradientUs_ > 4000.0) {
        // OVERUSE DETECTED: Network queue is buffering packets at router (>4ms gradient)
        // Multiplicative Decrease (back off by 15%)
        if (nowMs - lastBitrateUpdateMs_ >= 300) {
            targetBps = (std::max)(800000u, static_cast<uint32_t>(currentBps * 0.85));
            currentEstimatedBps_.store(targetBps, std::memory_order_relaxed);
            lastBitrateUpdateMs_ = nowMs;
            smoothedDelayGradientUs_ = 0.0; // Reset after backoff

            if (controlSendCallback_) {
                std::ostringstream ss;
                ss << "{\"type\":\"bitrate\",\"bps\":" << targetBps << "}";
                controlSendCallback_(ss.str());
            }
        }
    } else if (smoothedDelayGradientUs_ < 1000.0 && (nowMs - lastBitrateUpdateMs_ >= 1500)) {
        // UNDERUSE / NORMAL: Clean network, probe bandwidth with Additive Increase (+250 kbps)
        targetBps = (std::min)(8000000u, currentBps + 250000u);
        currentEstimatedBps_.store(targetBps, std::memory_order_relaxed);
        lastBitrateUpdateMs_ = nowMs;

        if (controlSendCallback_) {
            std::ostringstream ss;
            ss << "{\"type\":\"bitrate\",\"bps\":" << targetBps << "}";
            controlSendCallback_(ss.str());
        }
    }
}

void DcVideoDepacketizer::RequestKeyframe() {
    auto nowTime = std::chrono::steady_clock::now();
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(nowTime.time_since_epoch()).count();

    if (nowMs - lastPliSentMs_ >= 200) {
        lastPliSentMs_ = nowMs;
        if (controlSendCallback_) {
            controlSendCallback_("{\"type\":\"pli\"}");
        }
    }
}

void DcVideoDepacketizer::OnTimerTick() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (waitingForKeyframe_) {
        RequestKeyframe();
    }
}

} // namespace km::codec
