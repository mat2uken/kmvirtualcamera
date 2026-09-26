#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "../signaling/signaling_models.h"
#include "bandwidth_estimator.h"
#include "enhanced_rtcp_session.h"
#include "../../../shared/km/callback_gate.h"
#include "../../../shared/km/timing.h"
#include "../../../shared/receiver/audio/opus_rtp_decoder.h"
#include "../../../shared/receiver/engine/receiver_engine.h"
namespace rtc { class PeerConnection; class Track; class DataChannel; }
namespace km::rtc_net {
enum class PeerState { New, Connecting, Connected, Disconnected, Failed, Closed };
// Both transports now supply unwrapped media time in microseconds, not host time.
using VideoFrameCallback = std::function<void(const uint8_t*, size_t, int, int, int64_t)>;
// size_t is the total number of interleaved int16_t elements.
using AudioPcmCallback = std::function<void(const int16_t*, size_t, int, int)>;
using StateChangeCallback = std::function<void(PeerState)>;
class PeerConnectionManager {
public:
    PeerConnectionManager();
    ~PeerConnectionManager();
    bool Initialize(const signaling::RtcConfiguration&, StateChangeCallback, VideoFrameCallback, AudioPcmCallback);
    bool ProcessOfferAndGenerateAnswer(const std::string& offerSdp, std::string& answer);
    void RequestBitrate(uint32_t bitrate);
    void RequestKeyframe();
    uint32_t GetEstimatedBitrate() const;
    uint32_t GetMeasuredThroughput() const;
    float GetLossRatio() const;
    void SendControlMessage(const std::string& json);
    void SetControlMessageCallback(std::function<void(const std::string&)> callback);
    void CancelPending();
    // Control thread only, never from a callback. Callbacks must post lifecycle work.
    void Close();
private:
    void CloseInternal();
    mutable std::recursive_mutex rtcMutex_;
    std::mutex lifecycleMutex_;
    std::shared_ptr<km::CallbackGate> gate_;
    std::jthread timer_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::vector<std::shared_ptr<rtc::Track>> allTracks_;
    std::shared_ptr<rtc::Track> videoTrack_, audioTrack_;
    std::shared_ptr<EnhancedRtcpReceivingSession> videoRtcpSession_, audioRtcpSession_;
    std::shared_ptr<rtc::DataChannel> videoDc_, controlDc_;
    std::shared_ptr<BandwidthEstimator> bandwidthEstimator_;
    // Video path policy (payload filter, SSRC unwrap, RTP/DC arbitration, keyframe
    // throttle, frame delivery) lives in the shared engine; this class keeps RTC,
    // bandwidth, Opus and control wiring.
    km::engine::ReceiverEngine engine_;
    audio::OpusRtpDecoder opusDecoder_;
    StateChangeCallback stateCallback_;
    VideoFrameCallback videoCallback_;
    AudioPcmCallback audioCallback_;
    std::function<void(const std::string&)> controlCallback_;
    std::atomic<bool> gatheringComplete_{false}, cancelled_{false};
    std::atomic<uint64_t> sessionSerial_{0};
};
} // namespace km::rtc_net
