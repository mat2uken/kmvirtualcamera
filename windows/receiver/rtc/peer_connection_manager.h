#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include "../signaling/signaling_models.h"

#include "../codec/h264_rtp_depacketizer.h"
#include "../codec/dc_video_depacketizer.h"
#include "bandwidth_estimator.h"
#include "enhanced_rtcp_session.h"

// Forward declarations if libdatachannel is compiled conditionally or dynamically
namespace rtc {
    class PeerConnection;
    class Track;
    class DataChannel;
}

namespace km::rtc_net {

enum class PeerState {
    New,
    Connecting,
    Connected,
    Disconnected,
    Failed,
    Closed
};

using VideoFrameCallback = std::function<void(const uint8_t* data, size_t size, int width, int height, int64_t timestampUs)>;
using AudioPcmCallback = std::function<void(const int16_t* pcm, size_t samples, int channels, int sampleRate)>;
using StateChangeCallback = std::function<void(PeerState state)>;

class PeerConnectionManager {
public:
    PeerConnectionManager();
    ~PeerConnectionManager();

    bool Initialize(
        const signaling::RtcConfiguration& config,
        StateChangeCallback stateCb,
        VideoFrameCallback videoCb,
        AudioPcmCallback audioCb
    );

    // Applies remote Offer SDP, generates Answer, and waits for Non-Trickle ICE gathering complete
    bool ProcessOfferAndGenerateAnswer(const std::string& offerSdp, std::string& outAnswerSdp);

    void RequestBitrate(uint32_t bitrateBps);
    uint32_t GetEstimatedBitrate() const;
    uint32_t GetMeasuredThroughput() const;
    float GetLossRatio() const;

    void Close();

private:
    std::mutex rtcMutex_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::vector<std::shared_ptr<rtc::Track>> allTracks_;
    std::shared_ptr<rtc::Track> videoTrack_;
    std::shared_ptr<rtc::Track> audioTrack_;
    std::shared_ptr<EnhancedRtcpReceivingSession> videoRtcpSession_;
    codec::H264RtpDepacketizer h264Depacketizer_;
    codec::DcVideoDepacketizer dcVideoDepacketizer_;
    BandwidthEstimator bandwidthEstimator_;

    std::shared_ptr<rtc::DataChannel> videoDc_;
    std::shared_ptr<rtc::DataChannel> controlDc_;

    StateChangeCallback stateCallback_;
    VideoFrameCallback videoCallback_;
    AudioPcmCallback audioCallback_;

    std::atomic<bool> isGatheringComplete_{false};
    std::atomic<uint8_t> transportCcExtId_{0};
};

} // namespace km::rtc_net
