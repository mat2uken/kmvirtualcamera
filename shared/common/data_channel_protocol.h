#pragma once
#include <cstdint>
#include <cstddef>
namespace km::dc_protocol {
// Legacy wire layout. Parse bytewise in the receiver; never dereference this on the wire.
#pragma pack(push, 1)
struct DcPacketHeader {
    uint16_t magic;
    uint8_t flags, payloadType, chunkIndex, totalChunks;
    uint16_t frameSeq;
    uint32_t timestampUs;
};
#pragma pack(pop)
static_assert(sizeof(DcPacketHeader)==12);
inline constexpr uint16_t kDcMagic=0x4B4D;
enum DcHeaderFlags : uint8_t {
    kFlagKeyframe=1,kFlagHasConfig=2,kFlagFirstChunk=4,kFlagLastChunk=8
};
enum DcPayloadType : uint8_t { kPayloadH264=1,kPayloadOpus=2 };
// Match cloud/web/src/webcodecs_sender.ts: 1180 payload + 12 header.
// This is NOT a claim about one UDP datagram after SCTP/DTLS overhead.
inline constexpr size_t kMaxDcChunkPayload=1180;
inline constexpr size_t kMaxDcPacketSize=12+kMaxDcChunkPayload;
inline constexpr char kDataChannelVideo[]="km-video-stream";
inline constexpr char kDataChannelControl[]="km-control";
} // namespace km::dc_protocol
