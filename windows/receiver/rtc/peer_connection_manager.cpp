#include "peer_connection_manager.h"
#include <rtc/rtc.hpp>
#include <rtc/h264rtpdepacketizer.hpp>
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

    static bool sLoggerInit = false;
    if (!sLoggerInit) {
        sLoggerInit = true;
        rtc::InitLogger(rtc::LogLevel::Debug, [](rtc::LogLevel level, std::string message) {
            std::cout << "[libdatachannel] " << message << std::endl;
        });
    }

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

    // Add redundant global STUN servers
    rtcConfig.iceServers.emplace_back("stun:stun.l.google.com:19302");
    rtcConfig.iceServers.emplace_back("stun:stun1.l.google.com:19302");
    rtcConfig.iceServers.emplace_back("stun:stun2.l.google.com:19302");
    rtcConfig.iceServers.emplace_back("stun:stun3.l.google.com:19302");
    rtcConfig.iceServers.emplace_back("stun:stun4.l.google.com:19302");
    rtcConfig.iceServers.emplace_back("stun:stun.cloudflare.com:3478");
    rtcConfig.iceServers.emplace_back("stun:global.stun.twilio.com:3478");

    try {
        pc_ = std::make_shared<rtc::PeerConnection>(rtcConfig);

        pc_->onStateChange([this](rtc::PeerConnection::State state) {
            PeerState s = PeerState::New;
            switch (state) {
                case rtc::PeerConnection::State::New: s = PeerState::New; break;
                case rtc::PeerConnection::State::Connecting: s = PeerState::Connecting; break;
                case rtc::PeerConnection::State::Connected: {
                    s = PeerState::Connected;
                    if (videoTrack_) {
                        videoTrack_->requestKeyframe();
                    }
                    break;
                }
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

        h264Depacketizer_.Reset();
        h264Depacketizer_.SetCallback([this](const uint8_t* nalData, size_t size, uint32_t ts) {
            if (videoCallback_ && size > 0) {
                videoCallback_(nalData, size, 1280, 720, static_cast<int64_t>(ts));
            }
        });
        h264Depacketizer_.SetKeyframeRequestCallback([this]() {
            if (videoTrack_) {
                videoTrack_->requestKeyframe();
            }
        });

        pc_->onTrack([this](std::shared_ptr<rtc::Track> track) {
            allTracks_.push_back(track);

            auto media = track->description();
            std::string typeStr = media.type();
            std::string descStr = media.description();

            if (typeStr == "video" || descStr.find("video") != std::string::npos || descStr.find("H264") != std::string::npos || descStr.find("h264") != std::string::npos) {
                videoTrack_ = track;

                videoTrack_->onMessage([this](rtc::message_variant msg) {
                    if (std::holds_alternative<rtc::binary>(msg)) {
                        const auto& bin = std::get<rtc::binary>(msg);
                        h264Depacketizer_.ProcessRtpPacket(reinterpret_cast<const uint8_t*>(bin.data()), bin.size());
                    }
                });

                videoTrack_->requestKeyframe();
            } else if (typeStr == "audio" || descStr.find("audio") != std::string::npos || descStr.find("opus") != std::string::npos) {
                audioTrack_ = track;
                audioTrack_->onFrame([this](rtc::binary frame, rtc::FrameInfo info) {
                    if (audioCallback_ && !frame.empty()) {
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

        // Apply remote Offer. In RFC 8842, the browser offer has a=setup:actpass.
        // By specifying Role::Active for the remote offer, libdatachannel designates
        // itself as Role::Passive (DTLS Server, mIsClient = false) and produces a valid
        // Answer SDP with a=setup:passive.
        pc_->setRemoteDescription(rtc::Description(offerSdp, rtc::Description::Type::Offer, rtc::Description::Role::Active));

        if (pc_->gatheringState() == rtc::PeerConnection::GatheringState::Complete) {
            isGatheringComplete_ = true;
        }

        // Wait for ICE gathering to complete (Non-Trickle ICE)
        auto startTime = std::chrono::steady_clock::now();
        while (!isGatheringComplete_) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
            if (elapsed > 10000) {
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
    allTracks_.clear();
    videoTrack_.reset();
    audioTrack_.reset();
    h264Depacketizer_.Reset();
}

} // namespace km::rtc_net
