#include "dc_video_depacketizer.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>

namespace km::codec {

static const uint8_t kStartCode[4] = { 0x00, 0x00, 0x00, 0x01 };

static bool HasSpsPps(const uint8_t* data, size_t size) {
    if (size < 5) return false;
    for (size_t i = 0; i + 4 < size; ++i) {
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 1) {
            uint8_t naluType = data[i + 4] & 0x1F;
            if (naluType == 7) return true; // SPS
        }
    }
    return false;
}

static void NormalizeToAnnexB(
    const std::vector<uint8_t>& inBuf,
    std::vector<uint8_t>& outBuf,
    const std::vector<uint8_t>& cachedSpsPps,
    bool isKeyframe
) {
    if (inBuf.size() < 4) {
        outBuf = inBuf;
        return;
    }

    bool isAnnexB = (inBuf[0] == 0 && inBuf[1] == 0 && inBuf[2] == 0 && inBuf[3] == 1) ||
                    (inBuf[0] == 0 && inBuf[1] == 0 && inBuf[2] == 1);

    if (isAnnexB) {
        if (isKeyframe && !HasSpsPps(inBuf.data(), inBuf.size()) && !cachedSpsPps.empty()) {
            outBuf.clear();
            outBuf.insert(outBuf.end(), cachedSpsPps.begin(), cachedSpsPps.end());
            outBuf.insert(outBuf.end(), inBuf.begin(), inBuf.end());
        } else {
            outBuf = inBuf;
        }
        return;
    }

    // Convert AVCC (4-byte length prefix) to Annex-B (00 00 00 01)
    outBuf.clear();
    if (isKeyframe && !cachedSpsPps.empty()) {
        outBuf.insert(outBuf.end(), cachedSpsPps.begin(), cachedSpsPps.end());
    }

    size_t offset = 0;
    while (offset + 4 <= inBuf.size()) {
        uint32_t naluLen = (static_cast<uint32_t>(inBuf[offset]) << 24) |
                           (static_cast<uint32_t>(inBuf[offset + 1]) << 16) |
                           (static_cast<uint32_t>(inBuf[offset + 2]) << 8) |
                           (static_cast<uint32_t>(inBuf[offset + 3]));
        offset += 4;
        if (offset + naluLen > inBuf.size()) {
            break;
        }
        outBuf.insert(outBuf.end(), kStartCode, kStartCode + 4);
        outBuf.insert(outBuf.end(), inBuf.begin() + offset, inBuf.begin() + offset + naluLen);
        offset += naluLen;
    }

    if (outBuf.empty()) {
        outBuf = inBuf;
    }
}

DcVideoDepacketizer::DcVideoDepacketizer() {
    assemblyBuffer_.reserve(256 * 1024);
}

void DcVideoDepacketizer::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingFramesMap_.clear();
    assemblyBuffer_.clear();
    cachedSpsPps_.clear();
    hasInitializedSeq_ = false;
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

    uint16_t seq = header->frameSeq;

    // Discard stale packets for frames that have already been played out
    if (hasInitializedSeq_) {
        int16_t diff = static_cast<int16_t>(seq - expectedSeq_);
        if (diff < -50) {
            return; // Far in the past (already emitted), ignore
        }
    }

    // Lookup or create entry in pending frames map
    auto& frame = pendingFramesMap_[seq];
    if (frame.chunks.empty()) {
        frame.frameSeq = seq;
        frame.timestampUs = header->timestampUs;
        frame.totalChunks = header->totalChunks;
        frame.receivedChunks = 0;
        frame.isKeyframe = (header->flags & dc_protocol::kFlagKeyframe) != 0;
        frame.isComplete = false;
        frame.firstChunkArrivalUs = nowUs;
        frame.chunks.resize(header->totalChunks);
    }

    if (frame.chunks[header->chunkIndex].empty()) {
        frame.chunks[header->chunkIndex].assign(payload, payload + payloadSize);
        frame.receivedChunks++;
        if (frame.receivedChunks == frame.totalChunks) {
            frame.isComplete = true;
        }
    }

    // Drain and emit completed frames in strict sequence order
    DrainCompletedFrames(nowUs);
}

void DcVideoDepacketizer::DrainCompletedFrames(int64_t nowUs) {
    if (pendingFramesMap_.empty()) return;

    // 1. If not initialized yet, search for the first complete keyframe
    if (!hasInitializedSeq_) {
        for (auto it = pendingFramesMap_.begin(); it != pendingFramesMap_.end(); ++it) {
            if (it->second.isComplete && it->second.isKeyframe) {
                hasInitializedSeq_ = true;
                expectedSeq_ = it->first;
                break;
            }
        }
        if (!hasInitializedSeq_) {
            RequestKeyframe();
            return;
        }
    }

    // 2. Play out in-sequence frames (expectedSeq_, expectedSeq_+1, ...)
    while (!pendingFramesMap_.empty()) {
        auto it = pendingFramesMap_.find(expectedSeq_);
        if (it != pendingFramesMap_.end()) {
            if (it->second.isComplete) {
                EmitFrame(it->second, nowUs);
                pendingFramesMap_.erase(it);
                expectedSeq_++;
                continue;
            } else {
                // Frame exists but still waiting for remaining chunks.
                // Allow up to 10ms jitter window for packet reordering.
                if ((nowUs - it->second.firstChunkArrivalUs) < 10000) {
                    break; // Wait for in-flight chunks
                }
                // Deadline exceeded: Frame declared lost.
                pendingFramesMap_.erase(it);
                expectedSeq_++;
                waitingForKeyframe_ = true;
                RequestKeyframe();
                continue;
            }
        }

        // What if expectedSeq_ is not in pending map at all?
        // Check if a newer keyframe has arrived to immediately fast-forward
        bool foundNewerKeyframe = false;
        for (auto kit = pendingFramesMap_.begin(); kit != pendingFramesMap_.end(); ++kit) {
            if (kit->second.isComplete && kit->second.isKeyframe) {
                int16_t diff = static_cast<int16_t>(kit->first - expectedSeq_);
                if (diff > 0) {
                    // Fast-forward expectedSeq_ to this fresh keyframe
                    expectedSeq_ = kit->first;
                    foundNewerKeyframe = true;
                    break;
                }
            }
        }

        if (foundNewerKeyframe) {
            continue;
        }

        // If the map only contains future frames with gap > 1
        if (!pendingFramesMap_.empty()) {
            auto oldestIt = pendingFramesMap_.begin();
            int16_t diff = static_cast<int16_t>(oldestIt->first - expectedSeq_);
            if (diff > 0 && (nowUs - oldestIt->second.firstChunkArrivalUs) > 10000) {
                // Skip missing gap
                expectedSeq_ = oldestIt->first;
                waitingForKeyframe_ = true;
                RequestKeyframe();
                continue;
            }
        }

        break;
    }

    // 3. Clean up stale frames older than 60ms
    for (auto it = pendingFramesMap_.begin(); it != pendingFramesMap_.end(); ) {
        int16_t diff = static_cast<int16_t>(it->first - expectedSeq_);
        if (diff < 0 || (nowUs - it->second.firstChunkArrivalUs) > 60000) {
            it = pendingFramesMap_.erase(it);
        } else {
            ++it;
        }
    }
}

void DcVideoDepacketizer::EmitFrame(IncompleteDcFrame& frame, int64_t nowUs) {
    std::vector<uint8_t> rawAssembled;
    rawAssembled.reserve(64 * 1024);

    for (const auto& chunk : frame.chunks) {
        rawAssembled.insert(rawAssembled.end(), chunk.begin(), chunk.end());
    }

    if (rawAssembled.empty()) return;

    NormalizeToAnnexB(rawAssembled, assemblyBuffer_, cachedSpsPps_, frame.isKeyframe);

    if (frame.isKeyframe) {
        if (HasSpsPps(assemblyBuffer_.data(), assemblyBuffer_.size())) {
            cachedSpsPps_.assign(assemblyBuffer_.begin(), assemblyBuffer_.end());
        }
        waitingForKeyframe_ = false;
    }

    if (!waitingForKeyframe_ && frameCallback_) {
        frameCallback_(assemblyBuffer_.data(), assemblyBuffer_.size(), static_cast<int64_t>(frame.timestampUs));
        totalFramesAssembled_.fetch_add(1, std::memory_order_relaxed);
    }

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

    if (deltaArrivalUs <= 0 || deltaArrivalUs > 200000 || deltaSenderUs <= 0 || deltaSenderUs > 200000) {
        return;
    }

    int64_t delayGradientUs = deltaArrivalUs - deltaSenderUs;
    smoothedDelayGradientUs_ = 0.9 * smoothedDelayGradientUs_ + 0.1 * static_cast<double>(delayGradientUs);

    auto nowTime = std::chrono::steady_clock::now();
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(nowTime.time_since_epoch()).count();

    uint32_t currentBps = currentEstimatedBps_.load(std::memory_order_relaxed);
    uint32_t targetBps = currentBps;

    if (smoothedDelayGradientUs_ > 4000.0) {
        if (nowMs - lastBitrateUpdateMs_ >= 300) {
            targetBps = (std::max)(800000u, static_cast<uint32_t>(currentBps * 0.85));
            currentEstimatedBps_.store(targetBps, std::memory_order_relaxed);
            lastBitrateUpdateMs_ = nowMs;
            smoothedDelayGradientUs_ = 0.0;

            if (controlSendCallback_) {
                std::ostringstream ss;
                ss << "{\"type\":\"bitrate\",\"bps\":" << targetBps << "}";
                controlSendCallback_(ss.str());
            }
        }
    } else if (smoothedDelayGradientUs_ < 1000.0 && (nowMs - lastBitrateUpdateMs_ >= 1500)) {
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
    auto nowTime = std::chrono::steady_clock::now();
    int64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(nowTime.time_since_epoch()).count();

    std::lock_guard<std::mutex> lock(mutex_);
    DrainCompletedFrames(nowUs);
    if (waitingForKeyframe_) {
        RequestKeyframe();
    }
}

} // namespace km::codec
