#include "twcc_receiver.h"
#include <algorithm>
#include <cstring>
#include <iostream>

namespace km::rtc_net {

void TwccReceiver::WriteBE16(uint8_t* buf, uint16_t val) {
    buf[0] = static_cast<uint8_t>(val >> 8);
    buf[1] = static_cast<uint8_t>(val);
}

void TwccReceiver::WriteBE32(uint8_t* buf, uint32_t val) {
    buf[0] = static_cast<uint8_t>(val >> 24);
    buf[1] = static_cast<uint8_t>(val >> 16);
    buf[2] = static_cast<uint8_t>(val >> 8);
    buf[3] = static_cast<uint8_t>(val);
}

void TwccReceiver::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& r : records_) {
        r = TransportPacketRecord{};
    }
    pendingBaseSeq_ = 0;
    pendingEndSeq_ = 0;
    hasPending_ = false;
    fbPacketCount_ = 0;
}

uint16_t TwccReceiver::GetPendingPacketCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasPending_) return 0;
    return static_cast<uint16_t>(pendingEndSeq_ - pendingBaseSeq_);
}

bool TwccReceiver::ParseTransportSequence(
    const uint8_t* rtp_data, size_t rtp_size,
    uint8_t extension_id,
    uint16_t& out_seq)
{
    if (!rtp_data || rtp_size < 12 || extension_id == 0 || extension_id > 14) {
        return false;
    }

    uint8_t byte0 = rtp_data[0];
    if (((byte0 >> 6) & 0x03) != 2) return false; // RTP version must be 2
    bool hasExtension = (byte0 & 0x10) != 0;
    if (!hasExtension) return false;

    uint8_t csrcCount = byte0 & 0x0F;
    size_t extOffset = 12 + csrcCount * 4;
    if (rtp_size < extOffset + 4) return false;

    // Check for one-byte header extension profile (0xBEDE)
    uint16_t profile = (static_cast<uint16_t>(rtp_data[extOffset]) << 8) |
                        static_cast<uint16_t>(rtp_data[extOffset + 1]);
    if (profile != 0xBEDE) return false;

    uint16_t extLenWords = (static_cast<uint16_t>(rtp_data[extOffset + 2]) << 8) |
                            static_cast<uint16_t>(rtp_data[extOffset + 3]);
    size_t extDataOffset = extOffset + 4;
    size_t extDataEnd = extDataOffset + extLenWords * 4;
    if (extDataEnd > rtp_size) return false;

    // Parse one-byte header extension elements
    size_t pos = extDataOffset;
    while (pos < extDataEnd) {
        uint8_t byte = rtp_data[pos];

        // Padding byte
        if (byte == 0) {
            pos++;
            continue;
        }

        // ID=15 is reserved / terminator
        uint8_t id = (byte >> 4) & 0x0F;
        if (id == 15) break;

        uint8_t len = (byte & 0x0F) + 1; // L field is length-1
        pos++; // skip the ID/L byte

        if (pos + len > extDataEnd) break;

        if (id == extension_id && len >= 2) {
            out_seq = (static_cast<uint16_t>(rtp_data[pos]) << 8) |
                       static_cast<uint16_t>(rtp_data[pos + 1]);
            return true;
        }

        pos += len;
    }

    return false;
}

void TwccReceiver::OnPacket(uint16_t transport_seq, int64_t arrival_time_us, uint16_t packet_size) {
    std::lock_guard<std::mutex> lock(mutex_);

    size_t idx = transport_seq % kRingSize;
    records_[idx].arrival_time_us = arrival_time_us;
    records_[idx].packet_size = packet_size;
    records_[idx].received = true;

    if (!hasPending_) {
        pendingBaseSeq_ = transport_seq;
        pendingEndSeq_ = transport_seq + 1;
        hasPending_ = true;
    } else {
        // Extend pending range using signed comparison for wrap-around
        int16_t diffFromBase = static_cast<int16_t>(transport_seq - pendingBaseSeq_);
        int16_t diffFromEnd = static_cast<int16_t>(transport_seq - pendingEndSeq_);

        if (diffFromBase < 0) {
            // Packet arrived before current base
            pendingBaseSeq_ = transport_seq;
        }
        if (diffFromEnd >= 0) {
            // Packet arrived at or after current end
            pendingEndSeq_ = transport_seq + 1;
        }
    }
}

std::vector<uint8_t> TwccReceiver::BuildFeedbackPacket(uint32_t sender_ssrc, uint32_t media_ssrc) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!hasPending_) {
        return {};
    }

    uint16_t baseSeq = pendingBaseSeq_;
    uint16_t packetStatusCount = static_cast<uint16_t>(
        static_cast<uint16_t>(pendingEndSeq_ - baseSeq));

    // Cap to reasonable maximum to avoid huge packets
    if (packetStatusCount > 512) {
        packetStatusCount = 512;
    }
    if (packetStatusCount == 0) {
        return {};
    }

    // Determine packet statuses and deltas
    // Status: 0 = not received, 1 = received small delta, 2 = received large delta
    struct PacketStatus {
        uint8_t status; // 0, 1, or 2
        int16_t delta_units; // in 250us units (only valid if status > 0)
    };
    std::vector<PacketStatus> statuses;
    statuses.reserve(packetStatusCount);

    int64_t referenceTimeUs = 0;
    bool hasReference = false;
    int64_t prevArrivalUs = 0;

    for (uint16_t i = 0; i < packetStatusCount; i++) {
        uint16_t seq = baseSeq + i;
        size_t idx = seq % kRingSize;
        auto& rec = records_[idx];

        if (!rec.received) {
            statuses.push_back({0, 0});
            continue;
        }

        if (!hasReference) {
            referenceTimeUs = rec.arrival_time_us;
            hasReference = true;
            prevArrivalUs = rec.arrival_time_us;
            statuses.push_back({1, 0}); // First packet: delta = 0
            continue;
        }

        int64_t deltaUs = rec.arrival_time_us - prevArrivalUs;
        int64_t deltaUnits = deltaUs / kTimeResolutionUs;
        prevArrivalUs = rec.arrival_time_us;

        if (deltaUnits >= 0 && deltaUnits <= kSmallDeltaMaxUnits) {
            statuses.push_back({1, static_cast<int16_t>(deltaUnits)});
        } else {
            // Large delta: clamp to 2-byte signed range
            int16_t clamped = static_cast<int16_t>(
                (std::max)(static_cast<int64_t>(-32768),
                (std::min)(static_cast<int64_t>(32767), deltaUnits)));
            statuses.push_back({2, clamped});
        }
    }

    if (!hasReference) {
        return {}; // No received packets in range
    }

    // Build packet chunks using 2-bit status vector chunks (7 symbols per chunk)
    std::vector<uint16_t> chunks;
    std::vector<uint8_t> recvDeltas;

    for (size_t i = 0; i < statuses.size(); ) {
        // Check if we can use a run-length chunk (all same status for >= 7 packets)
        uint8_t runStatus = statuses[i].status;
        size_t runLen = 1;
        while (i + runLen < statuses.size() && statuses[i + runLen].status == runStatus && runLen < 8191) {
            runLen++;
        }

        if (runLen >= 7) {
            // Run-length chunk: bit 15 = 0, bits 14-13 = status, bits 12-0 = run length
            uint16_t chunk = 0;
            chunk |= (static_cast<uint16_t>(runStatus) << 13);
            chunk |= static_cast<uint16_t>(runLen & 0x1FFF);
            chunks.push_back(chunk);

            // Add deltas for received packets in this run
            for (size_t j = 0; j < runLen; j++) {
                auto& s = statuses[i + j];
                if (s.status == 1) {
                    recvDeltas.push_back(static_cast<uint8_t>(s.delta_units & 0xFF));
                } else if (s.status == 2) {
                    uint8_t hi = static_cast<uint8_t>((static_cast<uint16_t>(s.delta_units) >> 8) & 0xFF);
                    uint8_t lo = static_cast<uint8_t>(static_cast<uint16_t>(s.delta_units) & 0xFF);
                    recvDeltas.push_back(hi);
                    recvDeltas.push_back(lo);
                }
            }
            i += runLen;
        } else {
            // 2-bit status vector chunk: bit 15 = 1, bit 14 = 1, bits 13-0 = 7 x 2-bit symbols
            uint16_t chunk = 0xC000; // 1 1 xxxxxxxxxxxxxx
            size_t count = (std::min)(static_cast<size_t>(7), statuses.size() - i);
            for (size_t j = 0; j < 7; j++) {
                uint8_t sym = (i + j < statuses.size()) ? statuses[i + j].status : 0;
                chunk |= (static_cast<uint16_t>(sym & 0x03) << (12 - j * 2));
            }
            chunks.push_back(chunk);

            // Add deltas
            for (size_t j = 0; j < count; j++) {
                auto& s = statuses[i + j];
                if (s.status == 1) {
                    recvDeltas.push_back(static_cast<uint8_t>(s.delta_units & 0xFF));
                } else if (s.status == 2) {
                    uint8_t hi = static_cast<uint8_t>((static_cast<uint16_t>(s.delta_units) >> 8) & 0xFF);
                    uint8_t lo = static_cast<uint8_t>(static_cast<uint16_t>(s.delta_units) & 0xFF);
                    recvDeltas.push_back(hi);
                    recvDeltas.push_back(lo);
                }
            }
            i += count;
        }
    }

    // Calculate reference time (24-bit signed, in 64ms units)
    int32_t refTime24 = static_cast<int32_t>(referenceTimeUs / kRefTimeResolutionUs) & 0x00FFFFFF;

    // Build the RTCP packet
    // Header: 4 bytes (V=2, P=0, FMT=15, PT=205, length)
    // Sender SSRC: 4 bytes
    // Media SSRC: 4 bytes
    // Base seq + status count: 4 bytes
    // Reference time + fb pkt count: 4 bytes
    // Chunks: 2 bytes each
    // Recv deltas: variable
    // Padding to 4-byte boundary

    size_t chunkBytes = chunks.size() * 2;
    size_t deltaBytes = recvDeltas.size();
    size_t payloadSize = 8 + 4 + 4 + chunkBytes + deltaBytes; // after sender/media SSRC
    // Pad to 4-byte boundary
    size_t totalPayload = payloadSize;
    size_t padding = (4 - (totalPayload % 4)) % 4;
    totalPayload += padding;

    size_t totalSize = 4 + 4 + 4 + totalPayload; // header + sender_ssrc + media_ssrc + payload
    // Actually: header(4) + sender_ssrc(4) + media_ssrc(4) + base_seq(2) + status_count(2) + ref_time(3) + fb_count(1) + chunks + deltas + padding
    totalSize = 4 + 4 + 4 + 4 + 4 + chunkBytes + deltaBytes + padding;

    std::vector<uint8_t> packet(totalSize, 0);
    uint8_t* p = packet.data();

    // RTCP header: V=2, P=0, FMT=15, PT=205
    p[0] = (2 << 6) | 15; // V=2, P=0, FMT=15
    p[1] = 205;            // PT = RTPFB
    // Length in 32-bit words minus 1
    uint16_t lengthWords = static_cast<uint16_t>((totalSize / 4) - 1);
    WriteBE16(p + 2, lengthWords);

    // Sender SSRC
    WriteBE32(p + 4, sender_ssrc);
    // Media source SSRC
    WriteBE32(p + 8, media_ssrc);

    // Base sequence number + packet status count
    WriteBE16(p + 12, baseSeq);
    WriteBE16(p + 14, packetStatusCount);

    // Reference time (24-bit) + fb packet count (8-bit)
    p[16] = static_cast<uint8_t>((refTime24 >> 16) & 0xFF);
    p[17] = static_cast<uint8_t>((refTime24 >> 8) & 0xFF);
    p[18] = static_cast<uint8_t>(refTime24 & 0xFF);
    p[19] = fbPacketCount_++;

    // Packet chunks
    size_t offset = 20;
    for (uint16_t chunk : chunks) {
        WriteBE16(p + offset, chunk);
        offset += 2;
    }

    // Recv deltas
    for (uint8_t delta : recvDeltas) {
        p[offset++] = delta;
    }

    // Clear reported records and update state
    for (uint16_t i = 0; i < packetStatusCount; i++) {
        uint16_t seq = baseSeq + i;
        records_[seq % kRingSize] = TransportPacketRecord{};
    }
    hasPending_ = false;

    return packet;
}

} // namespace km::rtc_net
