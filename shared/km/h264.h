#pragma once
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace km::h264 {
using Bytes = std::span<const uint8_t>;
inline constexpr size_t kMaxAccessUnitBytes = 4 * 1024 * 1024;
inline constexpr size_t kMaxNalus = 1024;
inline size_t StartCode(Bytes b, size_t p) {
    if (p + 3 <= b.size() && b[p] == 0 && b[p+1] == 0) {
        if (b[p+2] == 1) return 3;
        if (p + 4 <= b.size() && b[p+2] == 0 && b[p+3] == 1) return 4;
    }
    return 0;
}
inline bool ValidNalu(Bytes n) {
    return !n.empty() && !(n[0] & 0x80) && (n[0] & 31) > 0 && (n[0] & 31) < 24;
}
// Views borrow the input. Accept Annex-B only, never guess a truncated packet.
inline bool SplitAnnexB(Bytes b, std::vector<Bytes>& nalus) {
    nalus.clear();
    if (b.empty() || b.size() > kMaxAccessUnitBytes) return false;
    size_t p = 0;
    while (p < b.size() && !StartCode(b, p) && b[p] == 0) ++p;
    if (!StartCode(b, p)) return false;
    while (p < b.size()) {
        const size_t prefix = StartCode(b, p);
        if (!prefix) { nalus.clear(); return false; }
        const size_t begin = p + prefix;
        size_t next = begin;
        while (next < b.size() && !StartCode(b, next)) ++next;
        size_t end = next;
        while (end > begin && b[end-1] == 0) --end; // Annex-B trailing_zero_8bits
        Bytes n = b.subspan(begin, end-begin);
        if (!ValidNalu(n) || nalus.size() == kMaxNalus) { nalus.clear(); return false; }
        nalus.push_back(n);
        p = next;
    }
    return !nalus.empty();
}
inline bool SplitLengthPrefixed4(Bytes b, std::vector<Bytes>& nalus) {
    nalus.clear();
    if (b.empty() || b.size() > kMaxAccessUnitBytes) return false;
    size_t p = 0;
    while (p < b.size()) {
        if (b.size()-p < 4) { nalus.clear(); return false; }
        uint32_t n = (uint32_t(b[p])<<24) | (uint32_t(b[p+1])<<16) |
                     (uint32_t(b[p+2])<<8) | b[p+3];
        p += 4;
        if (!n || n > b.size()-p || nalus.size() == kMaxNalus || !ValidNalu(b.subspan(p,n))) {
            nalus.clear(); return false;
        }
        nalus.push_back(b.subspan(p,n)); p += n;
    }
    return !nalus.empty();
}
inline void AppendAnnexB(std::vector<uint8_t>& out, Bytes n) {
    out.insert(out.end(), {0,0,0,1}); out.insert(out.end(), n.begin(), n.end());
}
inline bool AnnexBToLengthPrefixed4(Bytes input, std::vector<uint8_t>& out) {
    std::vector<Bytes> nalus;
    out.clear();
    if (!SplitAnnexB(input, nalus)) return false;
    for (Bytes n : nalus) {
        const auto size = static_cast<uint32_t>(n.size());
        out.insert(out.end(), {uint8_t(size>>24),uint8_t(size>>16),uint8_t(size>>8),uint8_t(size)});
        out.insert(out.end(), n.begin(), n.end());
    }
    return true;
}
} // namespace km::h264
