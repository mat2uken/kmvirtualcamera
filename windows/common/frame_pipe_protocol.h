#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>
#include <array>
#include <span>
#include <optional>

namespace km::protocol {

inline constexpr std::string_view kPipeName = R"(\\.\pipe\WebRtcBridge.VirtualCamera.v1)";
inline constexpr std::array<uint8_t, 8> kMagic = {'W', 'R', 'T', 'C', 'V', 'F', '0', '1'};
inline constexpr uint16_t kProtocolVersion = 1;
inline constexpr uint16_t kHeaderSize = 64;

inline constexpr uint32_t kWidth = 1280;
inline constexpr uint32_t kHeight = 720;
inline constexpr uint32_t kFourCcNv12 = 0x3231564E; // 'NV12' in Little-Endian ('N' | ('V' << 8) | ('1' << 16) | ('2' << 24))
inline constexpr uint32_t kStrideY = 1280;
inline constexpr uint32_t kStrideUv = 1280;
inline constexpr uint32_t kPayloadBytes = (kStrideY * kHeight) + (kStrideUv * (kHeight / 2)); // 1,382,400 bytes

// Flags
inline constexpr uint32_t kFlagDiscontinuity = 1u << 0;
inline constexpr uint32_t kFlagKeyVisual = 1u << 1;
inline constexpr uint32_t kFlagCrcPresent = 1u << 2;
inline constexpr uint32_t kFlagSourceMuted = 1u << 3;

#pragma pack(push, 1)
struct FrameHeader {
    uint8_t magic[8];
    uint16_t version;
    uint16_t headerSize;
    uint32_t flags;
    uint64_t sequence;
    int64_t captureTimeUs;
    uint32_t width;
    uint32_t height;
    uint32_t fourcc;
    uint32_t strideY;
    uint32_t strideUV;
    uint32_t payloadBytes;
    uint32_t headerCrc32;
    uint32_t payloadCrc32;
};
#pragma pack(pop)

static_assert(sizeof(FrameHeader) == kHeaderSize, "FrameHeader must be exactly 64 bytes");

inline void SerializeHeader(const FrameHeader& src, std::span<uint8_t, kHeaderSize> dst) noexcept {
    std::memcpy(dst.data(), &src, kHeaderSize);
}

inline std::optional<FrameHeader> DeserializeAndValidateHeader(std::span<const uint8_t> buffer) noexcept {
    if (buffer.size() < kHeaderSize) {
        return std::nullopt;
    }

    FrameHeader header;
    std::memcpy(&header, buffer.data(), kHeaderSize);

    // Validate magic
    if (std::memcmp(header.magic, kMagic.data(), 8) != 0) {
        return std::nullopt;
    }

    // Validate version and headerSize
    if (header.version != kProtocolVersion || header.headerSize != kHeaderSize) {
        return std::nullopt;
    }

    // Validate video format properties
    if (header.width != kWidth || header.height != kHeight) {
        return std::nullopt;
    }
    if (header.fourcc != kFourCcNv12) {
        return std::nullopt;
    }
    if (header.strideY != kStrideY || header.strideUV != kStrideUv) {
        return std::nullopt;
    }
    if (header.payloadBytes != kPayloadBytes) {
        return std::nullopt;
    }

    // Validate reserved flags (bits 4..31 must be zero)
    if ((header.flags & ~0x0Fu) != 0) {
        return std::nullopt;
    }

    return header;
}

inline FrameHeader CreateDefaultHeader(uint64_t sequence, int64_t captureTimeUs = 0, uint32_t flags = 0) noexcept {
    FrameHeader h{};
    std::memcpy(h.magic, kMagic.data(), 8);
    h.version = kProtocolVersion;
    h.headerSize = kHeaderSize;
    h.flags = flags;
    h.sequence = sequence;
    h.captureTimeUs = captureTimeUs;
    h.width = kWidth;
    h.height = kHeight;
    h.fourcc = kFourCcNv12;
    h.strideY = kStrideY;
    h.strideUV = kStrideUv;
    h.payloadBytes = kPayloadBytes;
    h.headerCrc32 = 0;
    h.payloadCrc32 = 0;
    return h;
}

inline void FillBlackNv12(std::span<uint8_t> buffer) noexcept {
    if (buffer.size() < kPayloadBytes) return;
    // Y plane: 0x10 (black in standard studio swing) or 0x00
    std::memset(buffer.data(), 0x10, kStrideY * kHeight);
    // UV interleaved plane: 0x80 (neutral chrominance)
    std::memset(buffer.data() + (kStrideY * kHeight), 0x80, kStrideUv * (kHeight / 2));
}

} // namespace km::protocol
