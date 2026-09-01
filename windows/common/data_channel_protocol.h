#pragma once

#include <cstdint>
#include <cstddef>

namespace km::dc_protocol {

// 12-Byte Binary Packet Header for Unreliable Video Chunks over DataChannel
#pragma pack(push, 1)
struct DcPacketHeader {
    uint16_t magic;          // 0x4B4D ('KM')
    uint8_t flags;           // Bit 0: KEYFRAME, Bit 1: HAS_CONFIG (SPS/PPS), Bit 2: FIRST_CHUNK, Bit 3: LAST_CHUNK
    uint8_t payloadType;     // 1: H.264 Annex-B, 2: Opus Audio
    uint8_t chunkIndex;      // 0 .. totalChunks - 1
    uint8_t totalChunks;     // Total chunks for this frame
    uint16_t frameSeq;       // Monotonic frame sequence number (0 .. 65535)
    uint32_t timestampUs;    // Send/Capture timestamp in microseconds
};
#pragma pack(pop)

static_assert(sizeof(DcPacketHeader) == 12, "DcPacketHeader must be exactly 12 bytes");

inline constexpr uint16_t kDcMagic = 0x4B4D;

enum DcHeaderFlags : uint8_t {
    kFlagKeyframe   = 0x01,
    kFlagHasConfig  = 0x02,
    kFlagFirstChunk = 0x04,
    kFlagLastChunk  = 0x08,
};

enum DcPayloadType : uint8_t {
    kPayloadH264 = 1,
    kPayloadOpus = 2,
};

inline constexpr size_t kMaxDcChunkPayload = 1168; // 1180 - 12 (MTU safe chunk size)
inline constexpr size_t kMaxDcPacketSize = sizeof(DcPacketHeader) + kMaxDcChunkPayload;

// Control Channel Channel Label Names
inline constexpr char kDataChannelVideo[] = "km-video-stream";
inline constexpr char kDataChannelControl[] = "km-control";

} // namespace km::dc_protocol
