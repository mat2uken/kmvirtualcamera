#include "peer_connection_manager.h"
#include <rtc/rtc.hpp>
#include <rtc/h264rtpdepacketizer.hpp>
#include <rtc/rtcpreceivingsession.hpp>
#include <chrono>
#include <thread>
#include <iostream>
#include <sstream>

static uint8_t ParseTransportCcExtensionIdFromSdp(const std::string& sdp) {
    std::istringstream stream(sdp);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("a=extmap:", 0) == 0) {
            if (line.find("transport-wide-cc-extensions") != std::string::npos ||
                line.find("transport-cc") != std::string::npos ||
                line.find("transport-wide-cc-02") != std::string::npos) {
                size_t idStart = 9; // strlen("a=extmap:")
                size_t idEnd = line.find_first_of("/ \t", idStart);
                if (idEnd != std::string::npos) {
                    try {
                        int id = std::stoi(line.substr(idStart, idEnd - idStart));
                        if (id > 0 && id <= 14) {
                            return static_cast<uint8_t>(id);
                        }
                    } catch (...) {}
                }
            }
        }
    }
    return 0;
}

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

    stateCallback_ = stateCb;
    videoCallback_ = videoCb;
    audioCallback_ = audioCb;
    isGatheringComplete_ = false;

    rtc::Configuration rtcConfig;
    for (const auto& s : config.iceServers) {
        for (const auto& stunUrl : s.urls) {
            if (stunUrl.rfind("stun:", 0) == 0) {
                std::string hostPort = stunUrl.substr(5);
                auto colon = hostPort.find(':');
                if (colon != std::string::npos) {
                    std::string host = hostPort.substr(0, colon);
                    uint16_t port = static_cast<uint16_t>(std::stoi(hostPort.substr(colon + 1)));
                    rtcConfig.iceServers.emplace_back(host, port);
                } else {
                    rtcConfig.iceServers.emplace_back(hostPort, 3478);
                }
            }
        }
    }

    // Add redundant global STUN servers
    rtcConfig.iceServers.emplace_back("stun.l.google.com", 19302);
    rtcConfig.iceServers.emplace_back("stun1.l.google.com", 19302);
    rtcConfig.iceServers.emplace_back("stun2.l.google.com", 19302);
    rtcConfig.iceServers.emplace_back("stun3.l.google.com", 19302);
    rtcConfig.iceServers.emplace_back("stun4.l.google.com", 19302);

    try {
        pc_ = std::make_shared<rtc::PeerConnection>(rtcConfig);

        pc_->onStateChange([this](rtc::PeerConnection::State state) {
            if (stateCallback_) {
                switch (state) {
                    case rtc::PeerConnection::State::New:
                        stateCallback_(PeerState::New);
                        break;
                    case rtc::PeerConnection::State::Connecting:
                        stateCallback_(PeerState::Connecting);
                        break;
                    case rtc::PeerConnection::State::Connected:
                        stateCallback_(PeerState::Connected);
                        break;
                    case rtc::PeerConnection::State::Disconnected:
                        stateCallback_(PeerState::Disconnected);
                        break;
                    case rtc::PeerConnection::State::Failed:
                        stateCallback_(PeerState::Failed);
                        break;
                    case rtc::PeerConnection::State::Closed:
                        stateCallback_(PeerState::Closed);
                        break;
                }
            }
        });

        pc_->onGatheringStateChange([this](rtc::PeerConnection::GatheringState state) {
            if (state == rtc::PeerConnection::GatheringState::Complete) {
                isGatheringComplete_ = true;
            }
        });

        bandwidthEstimator_.Reset();
        h264Depacketizer_.Reset();
        h264Depacketizer_.SetCallback([this](const uint8_t* nalData, size_t size, uint32_t ts) {
            if (videoCallback_ && size > 0) {
                videoCallback_(nalData, size, 1280, 720, static_cast<int64_t>(ts));
            }
        });
        h264Depacketizer_.SetKeyframeRequestCallback([this]() {
            bandwidthEstimator_.OnLossEventDetected();
            if (videoTrack_) {
                videoTrack_->requestKeyframe();
                uint32_t reduced = bandwidthEstimator_.GetCurrentEstimatedBitrate();
                videoTrack_->requestBitrate(reduced);
            }
        });

        pc_->onTrack([this](std::shared_ptr<rtc::Track> track) {
            allTracks_.push_back(track);

            auto media = track->description();
            std::string typeStr = media.type();
            std::string descStr = media.description();

            if (typeStr == "video" || descStr.find("video") != std::string::npos || descStr.find("H264") != std::string::npos || descStr.find("h264") != std::string::npos) {
                videoTrack_ = track;

                // Attach Enhanced RTCP session for accurate RR, REMB timer, and TWCC feedback
                videoRtcpSession_ = std::make_shared<EnhancedRtcpReceivingSession>();
                videoRtcpSession_->SetBandwidthEstimator(&bandwidthEstimator_);
                videoRtcpSession_->SetTransportCcExtensionId(transportCcExtId_.load(std::memory_order_relaxed));
                videoTrack_->setMediaHandler(videoRtcpSession_);

                videoTrack_->onMessage([this](rtc::message_variant msg) {
                    if (std::holds_alternative<rtc::binary>(msg)) {
                        const auto& bin = std::get<rtc::binary>(msg);
                        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()
                        ).count();

                        if (bin.size() >= 12) {
                            const uint8_t* p = reinterpret_cast<const uint8_t*>(bin.data());
                            uint16_t seq = (static_cast<uint16_t>(p[2]) << 8) | p[3];
                            bandwidthEstimator_.OnRtpPacketReceived(bin.size(), seq, nowMs);
                        }

                        h264Depacketizer_.ProcessRtpPacket(reinterpret_cast<const uint8_t*>(bin.data()), bin.size());

                        uint32_t targetBps = 0;
                        if (bandwidthEstimator_.EvaluateEstimation(nowMs, targetBps)) {
                            if (videoTrack_) {
                                videoTrack_->requestBitrate(targetBps);
                            }
                        }
                    }
                });

                videoTrack_->requestKeyframe();
                videoTrack_->requestBitrate(bandwidthEstimator_.GetCurrentEstimatedBitrate());
            } else if (typeStr == "audio" || descStr.find("audio") != std::string::npos || descStr.find("opus") != std::string::npos) {
                audioTrack_ = track;
                audioTrack_->setMediaHandler(std::make_shared<rtc::RtcpReceivingSession>());
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

        // Parse transport-cc extension ID from Offer SDP
        uint8_t extId = ParseTransportCcExtensionIdFromSdp(offerSdp);
        transportCcExtId_.store(extId, std::memory_order_relaxed);
        if (extId > 0) {
            std::cout << "[WebRTC] Negotiated TWCC extension ID: " << static_cast<int>(extId) << std::endl;
            if (videoRtcpSession_) {
                videoRtcpSession_->SetTransportCcExtensionId(extId);
            }
        }

        // Apply Offer SDP directly (preserving transport-cc for browser GCC)
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

void PeerConnectionManager::RequestBitrate(uint32_t bitrateBps) {
    if (videoTrack_) {
        videoTrack_->requestBitrate(bitrateBps);
    }
}

uint32_t PeerConnectionManager::GetEstimatedBitrate() const {
    return bandwidthEstimator_.GetCurrentEstimatedBitrate();
}

uint32_t PeerConnectionManager::GetMeasuredThroughput() const {
    return bandwidthEstimator_.GetMeasuredThroughputBps();
}

float PeerConnectionManager::GetLossRatio() const {
    return bandwidthEstimator_.GetCurrentLossRatio();
}

void PeerConnectionManager::Close() {
    if (pc_) {
        try {
            pc_->close();
        } catch (...) {}
        pc_.reset();
    }
    allTracks_.clear();
    videoRtcpSession_.reset();
    videoTrack_.reset();
    audioTrack_.reset();
    h264Depacketizer_.Reset();
    bandwidthEstimator_.Reset();
}

} // namespace km::rtc_net
