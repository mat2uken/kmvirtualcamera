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

// Strictly extracts ONLY SPS (type 7) and PPS (type 8) NALUs, NEVER slices (type 1/5)
static std::vector<uint8_t> ExtractSpsPpsOnly(const uint8_t* data, size_t size) {
    std::vector<uint8_t> spsPps;
    if (size < 5) return spsPps;

    size_t i = 0;
    while (i + 4 < size) {
        size_t startCodeLen = 0;
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 1) {
            startCodeLen = 4;
        } else if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            startCodeLen = 3;
        }

        if (startCodeLen > 0) {
            size_t naluStart = i + startCodeLen;
            uint8_t naluType = data[naluStart] & 0x1F;

            // Find next start code or end of buffer
            size_t nextStart = size;
            for (size_t j = naluStart; j + 3 < size; ++j) {
                if ((data[j] == 0 && data[j + 1] == 0 && data[j + 2] == 1) ||
                    (j + 4 < size && data[j] == 0 && data[j + 1] == 0 && data[j + 2] == 0 && data[j + 3] == 1)) {
                    nextStart = j;
                    break;
                }
            }

            if (naluType == 7 || naluType == 8) { // SPS (7) or PPS (8) ONLY
                spsPps.push_back(0);
                spsPps.push_back(0);
                spsPps.push_back(0);
                spsPps.push_back(1);
                spsPps.insert(spsPps.end(), data + naluStart, data + nextStart);
            }

            i = nextStart;
        } else {
            ++i;
        }
    }
    return spsPps;
}

static bool HasAud(const uint8_t* data, size_t size) {
    if (size < 5) return false;
    if (data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1) {
        return (data[4] & 0x1F) == 9;
    }
    if (data[0] == 0 && data[1] == 0 && data[2] == 1) {
        return (data[3] & 0x1F) == 9;
    }
    return false;
}

static bool IsAvccFormat(const uint8_t* data, size_t size) {
    if (size < 5) return false;
    size_t offset = 0;
    while (offset + 4 < size) {
        uint32_t len = (static_cast<uint32_t>(data[offset]) << 24) |
                       (static_cast<uint32_t>(data[offset + 1]) << 16) |
                       (static_cast<uint32_t>(data[offset + 2]) << 8) |
                       (static_cast<uint32_t>(data[offset + 3]));
        if (len == 0 || offset + 4 + len > size) {
            return false;
        }
        uint8_t naluHeader = data[offset + 4];
        if ((naluHeader & 0x80) != 0) return false;
        uint8_t naluType = naluHeader & 0x1F;
        if (naluType == 0 || naluType > 23) return false;
        offset += 4 + len;
    }
    return offset == size;
}

static void NormalizeToAnnexB(
    const std::vector<uint8_t>& inBuf,
    std::vector<uint8_t>& outBuf,
    const std::vector<uint8_t>& cachedSpsPps,
    bool isKeyframe
) {
    static const uint8_t kAud[6] = { 0x00, 0x00, 0x00, 0x01, 0x09, 0xF0 };

    if (inBuf.size() < 4) {
        outBuf = inBuf;
        return;
    }

    if (IsAvccFormat(inBuf.data(), inBuf.size())) {
        outBuf.clear();
        outBuf.insert(outBuf.end(), kAud, kAud + 6);

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
            if (offset + naluLen > inBuf.size()) break;
            outBuf.insert(outBuf.end(), kStartCode, kStartCode + 4);
            outBuf.insert(outBuf.end(), inBuf.begin() + offset, inBuf.begin() + offset + naluLen);
            offset += naluLen;
        }
        return;
    }

    // Already Annex-B
    if (isKeyframe && !HasSpsPps(inBuf.data(), inBuf.size()) && !cachedSpsPps.empty()) {
        outBuf.clear();
        outBuf.insert(outBuf.end(), cachedSpsPps.begin(), cachedSpsPps.end());
        outBuf.insert(outBuf.end(), inBuf.begin(), inBuf.end());
    } else {
        outBuf = inBuf;
    }

    if (!outBuf.empty() && !HasAud(outBuf.data(), outBuf.size())) {
        outBuf.insert(outBuf.begin(), kAud, kAud + 6);
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
            return;
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
                if ((nowUs - it->second.firstChunkArrivalUs) < 10000) {
                    break; // Wait up to 10ms for in-flight chunks
                }
                // Deadline exceeded: Frame declared lost.
                pendingFramesMap_.erase(it);
                expectedSeq_++;
                waitingForKeyframe_ = true;
                RequestKeyframe();
                continue;
            }
        }

        // Fast-forward if a newer keyframe is available
        bool foundNewerKeyframe = false;
        for (auto kit = pendingFramesMap_.begin(); kit != pendingFramesMap_.end(); ++kit) {
            if (kit->second.isComplete && kit->second.isKeyframe) {
                int16_t diff = static_cast<int16_t>(kit->first - expectedSeq_);
                if (diff > 0) {
                    expectedSeq_ = kit->first;
                    foundNewerKeyframe = true;
                    break;
                }
            }
        }

        if (foundNewerKeyframe) {
            continue;
        }

        if (!pendingFramesMap_.empty()) {
            auto oldestIt = pendingFramesMap_.begin();
            int16_t diff = static_cast<int16_t>(oldestIt->first - expectedSeq_);
            if (diff > 0 && (nowUs - oldestIt->second.firstChunkArrivalUs) > 10000) {
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
        // Extract ONLY SPS & PPS NALUs into cache (NEVER slices/IDR payload!)
        auto extracted = ExtractSpsPpsOnly(assemblyBuffer_.data(), assemblyBuffer_.size());
        if (!extracted.empty()) {
            cachedSpsPps_ = std::move(extracted);
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
