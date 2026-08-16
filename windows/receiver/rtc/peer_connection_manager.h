#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include "../signaling/signaling_models.h"

// Forward declarations if libdatachannel is compiled conditionally or dynamically
namespace rtc {
    class PeerConnection;
    class Track;
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

    void Close();

private:
    std::mutex rtcMutex_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<rtc::Track> videoTrack_;
    std::shared_ptr<rtc::Track> audioTrack_;

    StateChangeCallback stateCallback_;
    VideoFrameCallback videoCallback_;
    AudioPcmCallback audioCallback_;

    std::atomic<bool> isGatheringComplete_{false};
};

} // namespace km::rtc_net
