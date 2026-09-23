#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>
#include <mutex>

namespace km::rtc_net {

/// Record of a single transport-wide packet arrival
struct TransportPacketRecord {
    int64_t arrival_time_us = 0;    // Microseconds (steady_clock)
    uint16_t packet_size = 0;
    bool received = false;
};

/// Generates RTCP Transport-CC Feedback packets (RTPFB, PT=205, FMT=15)
/// per draft-holmer-rmcat-transport-wide-cc-extensions-01
///
/// The receiver records the arrival time of each transport-wide sequenced RTP packet,
/// and periodically builds feedback packets that the browser sender uses for
/// GCC (Google Congestion Control) bandwidth estimation.
class TwccReceiver {
public:
    static constexpr size_t kRingSize = 2048;
    static constexpr int64_t kTimeResolutionUs = 250;       // 250us per delta unit
    static constexpr int64_t kRefTimeResolutionUs = 64000;  // 64ms per reference time unit
    static constexpr int kSmallDeltaMaxUnits = 255;         // max 1-byte unsigned delta

    TwccReceiver() = default;
    ~TwccReceiver() = default;

    /// Record arrival of an RTP packet with the given transport-wide sequence number
    void OnPacket(uint16_t transport_seq, int64_t arrival_time_us, uint16_t packet_size);

    /// Build RTCP TWCC feedback packet. Returns empty vector if nothing to report.
    std::vector<uint8_t> BuildFeedbackPacket(uint32_t sender_ssrc, uint32_t media_ssrc);

    /// Return count of pending packets awaiting feedback
    uint16_t GetPendingPacketCount() const;

    /// Reset all state
    void Reset();

    /// Parse transport-wide sequence number from RTP one-byte header extensions (RFC 5285).
    /// Returns true if found, sets out_seq.
    static bool ParseTransportSequence(
        const uint8_t* rtp_data, size_t rtp_size,
        uint8_t extension_id,
        uint16_t& out_seq);

private:
    static void WriteBE16(uint8_t* buf, uint16_t val);
    static void WriteBE32(uint8_t* buf, uint32_t val);

    std::array<TransportPacketRecord, kRingSize> records_{};

    uint16_t pendingBaseSeq_ = 0;
    uint16_t pendingEndSeq_ = 0;     // exclusive: next expected seq after last received
    bool hasPending_ = false;
    uint8_t fbPacketCount_ = 0;

    mutable std::mutex mutex_;
};

} // namespace km::rtc_net
