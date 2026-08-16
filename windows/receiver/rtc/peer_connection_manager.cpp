#include "peer_connection_manager.h"
#include <rtc/rtc.hpp>
#include <chrono>
#include <thread>
#include <iostream>

namespace km::rtc_net {

PeerConnectionManager::PeerConnectionManager() = default;

PeerConnectionManager::~PeerConnectionManager() {
    Close();
}

bool PeerConnectionManager::Initialize(
    const signaling::RtcConfiguration& config,
    StateChangeCallback stateCb,
    VideoFrameCallback videoCb,
    AudioPcmCallback audioCb
) {
    std::lock_guard<std::mutex> lock(rtcMutex_);
    Close();

    stateCallback_ = std::move(stateCb);
    videoCallback_ = std::move(videoCb);
    audioCallback_ = std::move(audioCb);

    rtc::Configuration rtcConfig;
    for (const auto& s : config.iceServers) {
        for (const auto& url : s.urls) {
            rtc::IceServer server(url);
            if (!s.username.empty()) {
                server.username = s.username;
                server.password = s.credential;
            }
            rtcConfig.iceServers.push_back(server);
        }
    }

    try {
        pc_ = std::make_shared<rtc::PeerConnection>(rtcConfig);

        pc_->onStateChange([this](rtc::PeerConnection::State state) {
            PeerState s = PeerState::New;
            switch (state) {
                case rtc::PeerConnection::State::New: s = PeerState::New; break;
                case rtc::PeerConnection::State::Connecting: s = PeerState::Connecting; break;
                case rtc::PeerConnection::State::Connected: s = PeerState::Connected; break;
                case rtc::PeerConnection::State::Disconnected: s = PeerState::Disconnected; break;
                case rtc::PeerConnection::State::Failed: s = PeerState::Failed; break;
                case rtc::PeerConnection::State::Closed: s = PeerState::Closed; break;
            }
            if (stateCallback_) {
                stateCallback_(s);
            }
        });

        pc_->onGatheringStateChange([this](rtc::PeerConnection::GatheringState state) {
            if (state == rtc::PeerConnection::GatheringState::Complete) {
                isGatheringComplete_ = true;
            }
        });

        pc_->onTrack([this](std::shared_ptr<rtc::Track> track) {
            std::string desc = track->description();
            if (desc.find("video") != std::string::npos || track->mid() == "0") {
                videoTrack_ = track;
                videoTrack_->onFrame([this](rtc::binary frame, rtc::FrameInfo info) {
                    if (videoCallback_ && !frame.empty()) {
                        // Deliver frame to converter
                        videoCallback_(reinterpret_cast<const uint8_t*>(frame.data()), frame.size(), 1280, 720, info.timestamp);
                    }
                });
            } else {
                audioTrack_ = track;
                audioTrack_->onFrame([this](rtc::binary frame, rtc::FrameInfo info) {
                    if (audioCallback_ && !frame.empty()) {
                        // Deliver decoded / PCM frame
                        audioCallback_(reinterpret_cast<const int16_t*>(frame.data()), frame.size() / 2, 2, 48000);
                    }
                });
            }
        });

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize libdatachannel PeerConnection: " << e.what() << std::endl;
        return false;
    }
}

bool PeerConnectionManager::ProcessOfferAndGenerateAnswer(const std::string& offerSdp, std::string& outAnswerSdp) {
    std::lock_guard<std::mutex> lock(rtcMutex_);
    if (!pc_) return false;

    try {
        isGatheringComplete_ = false;

        // Apply remote Offer
        pc_->setRemoteDescription(rtc::Description(offerSdp, "offer"));

        // Wait for ICE gathering to complete (Non-Trickle ICE)
        auto startTime = std::chrono::steady_clock::now();
        while (!isGatheringComplete_) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
            if (elapsed > 15000) {
                break; // Timeout fallback
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        auto localDesc = pc_->localDescription();
        if (!localDesc.has_value()) {
            return false;
        }

        outAnswerSdp = std::string(localDesc.value());
        return !outAnswerSdp.empty();
    } catch (const std::exception& e) {
        std::cerr << "Error during Offer/Answer negotiation: " << e.what() << std::endl;
        return false;
    }
}

void PeerConnectionManager::Close() {
    if (pc_) {
        try {
            pc_->close();
        } catch (...) {}
        pc_.reset();
    }
    videoTrack_.reset();
    audioTrack_.reset();
}

} // namespace km::rtc_net
