#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace km::wire {
using Bytes = std::span<const uint8_t>;
inline uint16_t be16(const uint8_t* p) { return uint16_t((uint16_t(p[0]) << 8) | p[1]); }
inline uint32_t be32(const uint8_t* p) { return (uint32_t(be16(p)) << 16) | be16(p + 2); }
inline uint64_t be64(const uint8_t* p) { return (uint64_t(be32(p)) << 32) | be32(p + 4); }
inline int sequenceDelta(uint16_t a, uint16_t b) {
    const auto d = uint16_t(a - b);
    return d < 32768 ? int(d) : int(d) - 65536;
}
struct RtpPacket {
    Bytes payload;
    uint32_t timestamp = 0, ssrc = 0;
    uint16_t sequence = 0;
    uint8_t payloadType = 0;
    bool marker = false;
};
inline std::optional<RtpPacket> parseRtp(Bytes bytes) {
    if (bytes.size() < 12 || bytes.size() > 65535 || (bytes[0] >> 6) != 2) return {};
    size_t offset = 12 + size_t(bytes[0] & 15) * 4;
    if (offset > bytes.size()) return {};
    if (bytes[0] & 16) {
        if (bytes.size() - offset < 4) return {};
        const size_t length = 4 + size_t(be16(bytes.data() + offset + 2)) * 4;
        if (length > bytes.size() - offset) return {};
        offset += length;
    }
    size_t end = bytes.size();
    if (bytes[0] & 32) {
        const size_t padding = bytes.back();
        if (padding == 0 || padding > end - offset) return {};
        end -= padding;
    }
    if (end <= offset) return {};
    return RtpPacket{bytes.subspan(offset, end - offset), be32(bytes.data() + 4),
        be32(bytes.data() + 8), be16(bytes.data() + 2), uint8_t(bytes[1] & 127), bool(bytes[1] & 128)};
}
struct RtcpPacket { Bytes body; uint8_t type = 0, count = 0; };
// Validates the entire datagram before exposing any SR/RR fields. Reduced-size RTCP
// is accepted; an initial SR/RR is not required. Unknown types are skipped by callers.
inline bool parseRtcp(Bytes bytes, std::vector<RtcpPacket>& packets) {
    packets.clear();
    if (bytes.empty() || bytes.size() > 65535 || bytes.size() % 4) return false;
    size_t offset = 0;
    while (offset < bytes.size()) {
        auto fail = [&] { packets.clear(); return false; };
        if (bytes.size() - offset < 4 || (bytes[offset] >> 6) != 2 || packets.size() >= 64) return fail();
        const size_t length = (size_t(be16(bytes.data() + offset + 2)) + 1) * 4;
        if (length > bytes.size() - offset) return fail();
        size_t content = length;
        if (bytes[offset] & 32) {
            if (offset + length != bytes.size()) return fail(); // padding only on last compound member
            const size_t padding = bytes[offset + length - 1];
            if (!padding || padding > length - 4 || padding % 4) return fail();
            content -= padding;
        }
        const uint8_t type = bytes[offset + 1], count = bytes[offset] & 31;
        if (type < 192 || type > 223) return fail();
        size_t minimum = 4;
        if (type == 200) minimum = 28 + size_t(count) * 24;
        else if (type == 201) minimum = 8 + size_t(count) * 24;
        else if (type == 203) minimum = 4 + size_t(count) * 4;
        else if (type == 204 || type == 205 || type == 206) minimum = 12;
        else if (type == 207) minimum = 8;
        if (content < minimum) return fail();
        packets.push_back({bytes.subspan(offset, content), type, count});
        offset += length;
    }
    return true;
}
} // namespace km::wire
