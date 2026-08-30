#include "enhanced_rtcp_session.h"
#include "bandwidth_estimator.h"
#include <rtc/rtc.hpp>
#include <rtc/rtp.hpp>
#include <rtc/rtcpreceivingsession.hpp>
#include <algorithm>
#include <iostream>
#include <chrono>

namespace km::rtc_net {

EnhancedRtcpReceivingSession::EnhancedRtcpReceivingSession() {
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    lastRrSentMs_.store(nowMs);
    lastRembSentMs_.store(nowMs);
    lastTwccSentMs_.store(nowMs);
}

void EnhancedRtcpReceivingSession::SetBandwidthEstimator(BandwidthEstimator* estimator) {
    bandwidthEstimator_ = estimator;
}

void EnhancedRtcpReceivingSession::SetTransportCcExtensionId(uint8_t id) {
    transportCcExtId_.store(id);
    if (id > 0) {
        std::cout << "[EnhancedRTCP] Transport-CC enabled with extmap ID: " << static_cast<int>(id) << std::endl;
    }
}

void EnhancedRtcpReceivingSession::incoming(rtc::message_vector &messages, const rtc::message_callback &send) {
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        cachedSend_ = send;
    }

    auto nowTime = std::chrono::steady_clock::now();
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(nowTime.time_since_epoch()).count();
    int64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(nowTime.time_since_epoch()).count();

    rtc::message_vector result;
    uint8_t twccId = transportCcExtId_.load(std::memory_order_relaxed);

    for (auto& message : messages) {
        switch (message->type) {
        case rtc::Message::Binary: {
            if (message->size() < sizeof(rtc::RtpHeader)) {
                continue;
            }

            const uint8_t* rawData = reinterpret_cast<const uint8_t*>(message->data());
            auto rtp = reinterpret_cast<const rtc::RtpHeader*>(rawData);

            if (rtp->version() != 2) {
                continue;
            }

            if (rtp->payloadType() == 200 || rtp->payloadType() == 201) {
                continue; // RTCP multiplexed
            }

            mSsrc = rtp->ssrc();

            // Process stats for RTCP Receiver Report
            ProcessRtpStats(rawData, message->size(), nowUs);

            // If TWCC is negotiated, extract transport-wide sequence number
            if (twccId > 0) {
                uint16_t transportSeq = 0;
                if (TwccReceiver::ParseTransportSequence(rawData, message->size(), twccId, transportSeq)) {
                    twccReceiver_.OnPacket(transportSeq, nowUs, static_cast<uint16_t>(message->size()));
                }
            }

            result.push_back(std::move(message));
            break;
        }

        case rtc::Message::Control: {
            if (message->size() >= sizeof(rtc::RtcpHeader)) {
                auto header = reinterpret_cast<const rtc::RtcpHeader*>(message->data());
                if (header->payloadType() == 200) { // Sender Report (SR)
                    auto sr = reinterpret_cast<const rtc::RtcpSr*>(message->data());
                    mSsrc = sr->senderSSRC();
                    mSyncRTPTS = sr->rtpTimestamp();
                    mSyncNTPTS = sr->ntpTimestamp();

                    {
                        std::lock_guard<std::mutex> lock(statsMutex_);
                        lastSrNtp_ = sr->ntpTimestamp();
                        lastSrReceivedMs_ = nowMs;
                        hasLastSr_ = true;
                    }
                } else if (header->payloadType() == 201) { // Receiver Report
                    auto rr = reinterpret_cast<const rtc::RtcpRr*>(message->data());
                    mSsrc = rr->senderSSRC();
                }
            }
            break;
        }

        default:
            break;
        }
    }

    messages.swap(result);

    // Check if we need to send periodic feedback (TWCC / RR / REMB)
    CheckAndSendPeriodicFeedback(send, nowMs);
}

void EnhancedRtcpReceivingSession::ProcessRtpStats(const uint8_t* data, size_t size, int64_t nowUs) {
    if (size < 12) return;

    auto rtp = reinterpret_cast<const rtc::RtpHeader*>(data);
    uint16_t seq = rtp->seqNumber();
    uint32_t rtpTimestamp = rtp->timestamp();

    std::lock_guard<std::mutex> lock(statsMutex_);

    if (!hasFirstPacket_) {
        hasFirstPacket_ = true;
        baseSeq_ = seq;
        highestSeqReceived_ = seq;
        lastIntervalHighestSeq_ = seq;
        seqCycles_ = 0;
        totalPacketsReceived_ = 1;
        lastIntervalPacketsReceived_ = 1;

        lastRtpTimestamp_ = rtpTimestamp;
        lastArrivalTimeUs_ = nowUs;
        hasLastTimestamp_ = true;
        interarrivalJitter_ = 0.0;
        return;
    }

    totalPacketsReceived_++;
    lastIntervalPacketsReceived_++;

    // Sequence wrap-around handling
    int16_t diff = static_cast<int16_t>(seq - highestSeqReceived_);
    if (diff > 0) {
        if (seq < highestSeqReceived_) {
            // Wrapped 16-bit sequence space
            seqCycles_++;
        }
        highestSeqReceived_ = seq;
    }

    // Interarrival Jitter calculation per RFC 3550 Section 6.4.1
    // J(i) = J(i-1) + (|D(i-1,i)| - J(i-1)) / 16
    // D(i,j) = (R_j - R_i) - (S_j - S_i)
    // R_j - R_i is in 90kHz units for video: delta_us * 90 / 1000
    if (hasLastTimestamp_) {
        int64_t arrivalDiff90k = (nowUs - lastArrivalTimeUs_) * 90 / 1000;
        int32_t rtpDiff = static_cast<int32_t>(rtpTimestamp - lastRtpTimestamp_);
        int32_t d = static_cast<int32_t>(arrivalDiff90k) - rtpDiff;
        if (d < 0) d = -d;

        interarrivalJitter_ += (static_cast<double>(d) - interarrivalJitter_) / 16.0;
    }

    lastRtpTimestamp_ = rtpTimestamp;
    lastArrivalTimeUs_ = nowUs;
    hasLastTimestamp_ = true;
}

void EnhancedRtcpReceivingSession::SendEnhancedRR(const rtc::message_callback &send, int64_t nowMs) {
    if (mSsrc == 0) return;

    std::lock_guard<std::mutex> lock(statsMutex_);
    if (!hasFirstPacket_) return;

    // Calculate expected packets since start
    uint32_t extendedHighestSeq = (static_cast<uint32_t>(seqCycles_) << 16) | highestSeqReceived_;
    uint32_t totalExpected = extendedHighestSeq - baseSeq_ + 1;
    uint32_t cumulativeLost = (totalExpected > totalPacketsReceived_)
        ? (totalExpected - totalPacketsReceived_)
        : 0;

    // Interval fraction lost
    uint32_t intervalExpected = 0;
    int16_t seqDiff = static_cast<int16_t>(highestSeqReceived_ - lastIntervalHighestSeq_);
    if (seqDiff > 0) {
        intervalExpected = static_cast<uint32_t>(seqDiff);
    }
    uint32_t intervalLost = (intervalExpected > lastIntervalPacketsReceived_)
        ? (intervalExpected - lastIntervalPacketsReceived_)
        : 0;

    uint8_t fractionLost = 0;
    if (intervalExpected > 0) {
        fractionLost = static_cast<uint8_t>(
            (std::min)(255ULL, (static_cast<uint64_t>(intervalLost) * 256ULL) / intervalExpected)
        );
    }

    // Reset interval counters
    lastIntervalPacketsReceived_ = 0;
    lastIntervalHighestSeq_ = highestSeqReceived_;

    // Calculate DLSR (delay since last SR) in units of 1/65536 seconds
    uint32_t dlsr = 0;
    if (hasLastSr_ && lastSrReceivedMs_ > 0) {
        int64_t delayMs = nowMs - lastSrReceivedMs_;
        if (delayMs >= 0) {
            dlsr = static_cast<uint32_t>((delayMs * 65536) / 1000);
        }
    }

    auto message = rtc::make_message(rtc::RtcpRr::SizeWithReportBlocks(1), rtc::Message::Control);
    auto rr = reinterpret_cast<rtc::RtcpRr*>(message->data());
    rr->preparePacket(1, 1); // sender SSRC = 1 (receiver ID)

    auto block = rr->getReportBlock(0);
    block->preparePacket(
        mSsrc,
        cumulativeLost,
        totalExpected,
        highestSeqReceived_,
        seqCycles_,
        static_cast<uint32_t>(interarrivalJitter_),
        lastSrNtp_,
        dlsr
    );
    block->setPacketsLost(fractionLost, cumulativeLost);

    send(message);
}

void EnhancedRtcpReceivingSession::SendTwccFeedback(const rtc::message_callback &send) {
    if (mSsrc == 0) return;

    auto fbPacket = twccReceiver_.BuildFeedbackPacket(1, mSsrc);
    if (!fbPacket.empty()) {
        auto message = rtc::make_message(fbPacket.size(), rtc::Message::Control);
        std::memcpy(message->data(), fbPacket.data(), fbPacket.size());
        send(message);
    }
}

void EnhancedRtcpReceivingSession::CheckAndSendPeriodicFeedback(const rtc::message_callback &send, int64_t nowMs) {
    uint8_t twccId = transportCcExtId_.load(std::memory_order_relaxed);

    // 1. TWCC feedback (every 25ms or immediately upon 16-packet burst)
    if (twccId > 0) {
        int64_t lastTwcc = lastTwccSentMs_.load(std::memory_order_relaxed);
        uint16_t pendingCount = twccReceiver_.GetPendingPacketCount();
        if ((nowMs - lastTwcc >= kTwccIntervalMs && pendingCount > 0) || pendingCount >= 16) {
            lastTwccSentMs_.store(nowMs, std::memory_order_relaxed);
            SendTwccFeedback(send);
        }
    }

    // 2. Enhanced Receiver Report (every 500ms)
    int64_t lastRr = lastRrSentMs_.load(std::memory_order_relaxed);
    if (nowMs - lastRr >= kRrIntervalMs) {
        lastRrSentMs_.store(nowMs, std::memory_order_relaxed);
        SendEnhancedRR(send, nowMs);
    }

    // 3. Periodic REMB (every 1000ms)
    int64_t lastRemb = lastRembSentMs_.load(std::memory_order_relaxed);
    if (nowMs - lastRemb >= kRembIntervalMs) {
        lastRembSentMs_.store(nowMs, std::memory_order_relaxed);
        if (bandwidthEstimator_) {
            uint32_t targetBps = bandwidthEstimator_->GetCurrentEstimatedBitrate();
            if (targetBps > 0 && mSsrc != 0) {
                pushREMB(send, targetBps);
            }
        }
    }
}

void EnhancedRtcpReceivingSession::FlushFeedback() {
    rtc::message_callback sendCopy;
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        sendCopy = cachedSend_;
    }

    if (sendCopy) {
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
        CheckAndSendPeriodicFeedback(sendCopy, nowMs);
    }
}

bool EnhancedRtcpReceivingSession::requestBitrate(unsigned int bitrate, const rtc::message_callback &send) {
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        cachedSend_ = send;
    }
    mRequestedBitrate.store(bitrate);
    if (mSsrc != 0) {
        pushREMB(send, bitrate);
    }
    return true;
}

bool EnhancedRtcpReceivingSession::requestKeyframe(const rtc::message_callback &send) {
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        cachedSend_ = send;
    }
    if (mSsrc != 0) {
        // Send PLI (PT=206, FMT=1)
        pushPLI(send);

        // Also send FIR (Full Intra Request, PT=206, FMT=4) for immediate browser keyframe generation
        auto firMsg = rtc::make_message(sizeof(rtc::RtcpFbHeader) + sizeof(rtc::RtcpFirPart), rtc::Message::Control);
        auto *fir = reinterpret_cast<rtc::RtcpFir *>(firMsg->data());
        fir->header.header.prepareHeader(206, 4, static_cast<uint16_t>((sizeof(rtc::RtcpFbHeader) + sizeof(rtc::RtcpFirPart)) / 4 - 1));
        fir->header.setPacketSenderSSRC(1);
        fir->header.setMediaSourceSSRC(0);
        uint32_t ssrcVal = mSsrc;
        fir->parts[0].ssrc = ((ssrcVal & 0xFF000000u) >> 24) |
                             ((ssrcVal & 0x00FF0000u) >> 8)  |
                             ((ssrcVal & 0x0000FF00u) << 8)  |
                             ((ssrcVal & 0x000000FFu) << 24);
        fir->parts[0].seqNo = firSeqNo_.fetch_add(1, std::memory_order_relaxed);
        fir->parts[0].dummy1 = 0;
        fir->parts[0].dummy2 = 0;
        send(firMsg);
    }
    return true;
}

void EnhancedRtcpReceivingSession::RequestKeyframeDirect() {
    rtc::message_callback sendCopy;
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        sendCopy = cachedSend_;
    }
    if (sendCopy) {
        requestKeyframe(sendCopy);
    }
}

} // namespace km::rtc_net
