#include "h264_rtp_depacketizer.h"
#include <cstring>
#include <iostream>

namespace km::codec {

static const uint8_t kStartSequence[4] = { 0x00, 0x00, 0x00, 0x01 };

H264RtpDepacketizer::H264RtpDepacketizer(FrameCallback callback, KeyframeRequestCallback keyframeCb)
    : callback_(std::move(callback)), keyframeRequestCallback_(std::move(keyframeCb)) {
    accessUnitBuffer_.reserve(256 * 1024);
    fuBuffer_.reserve(256 * 1024);
    reconstructedFrameBuffer_.reserve(256 * 1024);
}

void H264RtpDepacketizer::Reset() {
    accessUnitBuffer_.clear();
    fuBuffer_.clear();
    cachedSps_.clear();
    cachedPps_.clear();
    reconstructedFrameBuffer_.clear();
    hasPendingTimestamp_ = false;
    hasLastSeq_ = false;
    isFuActive_ = false;
    frameHasLoss_ = false;
    isKeyframe_ = false;
    waitingForKeyframe_ = true;
    waitingKeyframeCount_ = 0;
}

void H264RtpDepacketizer::EmitAccessUnit() {
    if (!accessUnitBuffer_.empty()) {
        if (frameHasLoss_) {
            waitingForKeyframe_ = true;
            waitingKeyframeCount_ = 0;
            if (keyframeRequestCallback_) {
                keyframeRequestCallback_();
            }
        } else if (waitingForKeyframe_) {
            if (isKeyframe_) {
                // If SPS / PPS were cached but not in the IDR packet, prepend them
                reconstructedFrameBuffer_.clear();
                if (!cachedSps_.empty() && !cachedPps_.empty()) {
                    // Check if SPS is already at start of accessUnitBuffer_
                    bool hasSps = (accessUnitBuffer_.size() > 4 && (accessUnitBuffer_[4] & 0x1F) == 7);
                    if (!hasSps) {
                        reconstructedFrameBuffer_.insert(reconstructedFrameBuffer_.end(), kStartSequence, kStartSequence + 4);
                        reconstructedFrameBuffer_.insert(reconstructedFrameBuffer_.end(), cachedSps_.begin(), cachedSps_.end());
                        reconstructedFrameBuffer_.insert(reconstructedFrameBuffer_.end(), kStartSequence, kStartSequence + 4);
                        reconstructedFrameBuffer_.insert(reconstructedFrameBuffer_.end(), cachedPps_.begin(), cachedPps_.end());
                    }
                }
                reconstructedFrameBuffer_.insert(reconstructedFrameBuffer_.end(), accessUnitBuffer_.begin(), accessUnitBuffer_.end());

                waitingForKeyframe_ = false;
                waitingKeyframeCount_ = 0;
                if (callback_) {
                    callback_(reconstructedFrameBuffer_.data(), reconstructedFrameBuffer_.size(), currentTimestamp_);
                }
            } else {
                // Periodically re-request PLI/FIR every 3 dropped frames (~100ms) until IDR arrives
                if (++waitingKeyframeCount_ % 3 == 0) {
                    if (keyframeRequestCallback_) {
                        keyframeRequestCallback_();
                    }
                }
            }
        } else {
            // Normal clean stream frame
            if (callback_) {
                callback_(accessUnitBuffer_.data(), accessUnitBuffer_.size(), currentTimestamp_);
            }
        }
        accessUnitBuffer_.clear();
    }
    isKeyframe_ = false;
    frameHasLoss_ = false;
}

void H264RtpDepacketizer::ProcessRtpPacket(const uint8_t* rtpData, size_t size) {
    if (!rtpData || size < 12) {
        return; // Minimum RTP header size is 12 bytes
    }

    uint8_t byte0 = rtpData[0];
    uint8_t version = (byte0 >> 6) & 0x03;
    if (version != 2) {
        return; // Only RTP version 2 is supported
    }

    bool hasPadding = (byte0 & 0x20) != 0;
    bool hasExtension = (byte0 & 0x10) != 0;
    uint8_t csrcCount = byte0 & 0x0F;

    uint8_t byte1 = rtpData[1];
    bool markerBit = (byte1 & 0x80) != 0;

    uint16_t seq = (static_cast<uint16_t>(rtpData[2]) << 8) | static_cast<uint16_t>(rtpData[3]);
    if (hasLastSeq_) {
        uint16_t diff = seq - lastSequenceNumber_;
        if (diff > 0 && diff < 32768) {
            if (diff > 1) {
                // Packet loss detected in transit over network!
                frameHasLoss_ = true;
                isFuActive_ = false;
                fuBuffer_.clear();
                if (keyframeRequestCallback_) {
                    keyframeRequestCallback_();
                }
            }
            lastSequenceNumber_ = seq;
        } else {
            // Duplicate or late out-of-order packet (diff == 0 or diff >= 32768)
            // Do NOT regress lastSequenceNumber_ backwards!
        }
    } else {
        lastSequenceNumber_ = seq;
        hasLastSeq_ = true;
    }

    uint32_t timestamp = (static_cast<uint32_t>(rtpData[4]) << 24) |
                         (static_cast<uint32_t>(rtpData[5]) << 16) |
                         (static_cast<uint32_t>(rtpData[6]) << 8)  |
                         static_cast<uint32_t>(rtpData[7]);

    size_t headerOffset = 12 + (csrcCount * 4);
    if (size < headerOffset) {
        return;
    }

    // Parse Extension Header if present
    if (hasExtension) {
        if (size < headerOffset + 4) {
            return;
        }
        uint16_t extLengthWords = (static_cast<uint16_t>(rtpData[headerOffset + 2]) << 8) |
                                   static_cast<uint16_t>(rtpData[headerOffset + 3]);
        headerOffset += 4 + (extLengthWords * 4);
        if (size < headerOffset) {
            return;
        }
    }

    // Determine payload end (accounting for padding)
    size_t payloadEnd = size;
    if (hasPadding && size > headerOffset) {
        uint8_t padLen = rtpData[size - 1];
        if (padLen <= (size - headerOffset)) {
            payloadEnd -= padLen;
        }
    }

    if (headerOffset >= payloadEnd) {
        return; // Empty payload (e.g. keepalive/padding)
    }

    const uint8_t* payload = rtpData + headerOffset;
    size_t payloadSize = payloadEnd - headerOffset;

    // Check if timestamp changed, indicating a new access unit
    if (hasPendingTimestamp_ && timestamp != currentTimestamp_) {
        EmitAccessUnit();
    }
    currentTimestamp_ = timestamp;
    hasPendingTimestamp_ = true;

    uint8_t nalHeader = payload[0];
    uint8_t nalType = nalHeader & 0x1F;

    if (nalType >= 1 && nalType <= 23) {
        // Single NAL unit packet
        isFuActive_ = false;
        fuBuffer_.clear();

        if (nalType == 7) {
            cachedSps_.assign(payload, payload + payloadSize);
        } else if (nalType == 8) {
            cachedPps_.assign(payload, payload + payloadSize);
        } else if (nalType == 5) {
            isKeyframe_ = true;
        }

        accessUnitBuffer_.insert(accessUnitBuffer_.end(), kStartSequence, kStartSequence + 4);
        accessUnitBuffer_.insert(accessUnitBuffer_.end(), payload, payload + payloadSize);
    } else if (nalType == 24) {
        // STAP-A Aggregation Packet
        isFuActive_ = false;
        fuBuffer_.clear();

        size_t currOffset = 1; // Skip STAP-A header
        while (currOffset + 2 <= payloadSize) {
            uint16_t naluSize = (static_cast<uint16_t>(payload[currOffset]) << 8) |
                                 static_cast<uint16_t>(payload[currOffset + 1]);
            currOffset += 2;

            if (currOffset + naluSize > payloadSize) {
                break; // Corrupted STAP-A length
            }

            uint8_t innerNalType = payload[currOffset] & 0x1F;
            if (innerNalType == 7) {
                cachedSps_.assign(payload + currOffset, payload + currOffset + naluSize);
            } else if (innerNalType == 8) {
                cachedPps_.assign(payload + currOffset, payload + currOffset + naluSize);
            } else if (innerNalType == 5) {
                isKeyframe_ = true;
            }

            accessUnitBuffer_.insert(accessUnitBuffer_.end(), kStartSequence, kStartSequence + 4);
            accessUnitBuffer_.insert(accessUnitBuffer_.end(), payload + currOffset, payload + currOffset + naluSize);

            currOffset += naluSize;
        }
    } else if (nalType == 28) {
        // FU-A Fragmentation Unit
        if (payloadSize >= 2) {
            uint8_t fuIndicator = payload[0];
            uint8_t fuHeader = payload[1];
            bool isStart = (fuHeader & 0x80) != 0;
            bool isEnd = (fuHeader & 0x40) != 0;
            uint8_t originalNalType = fuHeader & 0x1F;
            uint8_t reconstructedNalHeader = (fuIndicator & 0xE0) | originalNalType;

            if (originalNalType == 5) {
                isKeyframe_ = true;
            }

            if (isStart) {
                isFuActive_ = true;
                fuBuffer_.clear();
                fuBuffer_.insert(fuBuffer_.end(), kStartSequence, kStartSequence + 4);
                fuBuffer_.push_back(reconstructedNalHeader);
                fuBuffer_.insert(fuBuffer_.end(), payload + 2, payload + payloadSize);
            } else if (isFuActive_) {
                fuBuffer_.insert(fuBuffer_.end(), payload + 2, payload + payloadSize);
            }

            if (isEnd && isFuActive_) {
                accessUnitBuffer_.insert(accessUnitBuffer_.end(), fuBuffer_.begin(), fuBuffer_.end());
                fuBuffer_.clear();
                isFuActive_ = false;
            }
        }
    }

    // If marker bit is set, the complete video frame access unit is ready
    if (markerBit) {
        EmitAccessUnit();
    }
}

} // namespace km::codec
