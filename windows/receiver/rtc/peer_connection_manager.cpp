#include "peer_connection_manager.h"
#include "../../../shared/km/bounded_video_queue.h"
#include "../../../shared/km/json.h"
#include "../../../shared/receiver/signaling/session_codec.h"
#include <rtc/rtc.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <iostream>
#include <utility>
#ifndef KM_TURN_TCP_TLS
#define KM_TURN_TCP_TLS 0
#endif
#ifndef KM_ENABLE_OPUS
#define KM_ENABLE_OPUS 0
#endif
namespace km::rtc_net {
namespace {
int64_t nowUs() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
std::string lower(std::string s) { for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }
// Codec this receiver answers with: H264 video always, Opus audio only in Opus builds.
bool isAnsweredCodec(const rtc::Description::Media& media, const rtc::Description::Media::RtpMap* map) {
    return map && ((media.type() == "video" && lower(map->format) == "h264" && map->clockRate == 90000) ||
        (KM_ENABLE_OPUS && media.type() == "audio" && lower(map->format) == "opus" && map->clockRate == 48000));
}
template<class F> auto guarded(std::weak_ptr<km::CallbackGate> weak, F callback) {
    return [weak, callback = std::move(callback)](auto&&... args) {
        auto gate = weak.lock(); if (!gate) return;
        auto lease = gate->enter(); if (!lease) return;
        try { callback(std::forward<decltype(args)>(args)...); }
        catch (const std::exception&) { std::cerr << "[RTC] Callback failed; media/control input rejected\n"; }
    };
}
}
PeerConnectionManager::PeerConnectionManager() : bandwidthEstimator_(std::make_shared<BandwidthEstimator>()) {}
PeerConnectionManager::~PeerConnectionManager() { Close(); }
bool PeerConnectionManager::Initialize(const signaling::RtcConfiguration& config, StateChangeCallback state, VideoFrameCallback video, AudioPcmCallback audio) {
    std::lock_guard lifecycle(lifecycleMutex_);
    CloseInternal();
    try {
        std::lock_guard lock(rtcMutex_);
        rtc::Configuration native;
        native.maxMessageSize = 1024 * 1024;
        if (config.iceTransportPolicy != "all" && config.iceTransportPolicy != "relay") throw std::invalid_argument("unsupported ICE policy");
        native.iceTransportPolicy = config.iceTransportPolicy == "relay" ? rtc::TransportPolicy::Relay : rtc::TransportPolicy::All;
        bool usableTurn = false;
        for (const auto& server : config.iceServers) for (const auto& url : server.urls) {
            if (!signaling::supportedIceUrl(url)) throw std::invalid_argument("unsupported ICE URL");
            rtc::IceServer ice(url); // library URL parser handles IPv6, ports and transport
            if (ice.type == rtc::IceServer::Type::Turn) {
                ice.username = server.username; ice.password = server.credential;
                if (ice.username.empty() || ice.password.empty()) throw std::invalid_argument("missing TURN credentials");
                if (!KM_TURN_TCP_TLS && ice.relayType != rtc::IceServer::RelayType::TurnUdp) {
                    std::cerr << "[RTC] TURN TCP/TLS skipped: this build uses libjuice. Use a UDP TURN endpoint or a matching libnice build.\n";
                    continue;
                }
                usableTurn = true;
            }
            native.iceServers.push_back(std::move(ice));
        }
        if (config.iceTransportPolicy == "relay" && !usableTurn) throw std::invalid_argument("relay policy has no supported TURN endpoint");
        cancelled_ = false; gatheringComplete_ = false;
        stateCallback_ = std::move(state); videoCallback_ = std::move(video); audioCallback_ = std::move(audio);
        bandwidthEstimator_ = std::make_shared<BandwidthEstimator>();
        gate_ = std::make_shared<km::CallbackGate>(); const std::weak_ptr<km::CallbackGate> weak = gate_;
        pc_ = std::make_shared<rtc::PeerConnection>(native);
        pc_->onStateChange(guarded(weak, [this](rtc::PeerConnection::State s) {
            std::lock_guard lock(rtcMutex_);
            PeerState value = PeerState::New;
            switch (s) {
            case rtc::PeerConnection::State::New: value = PeerState::New; break;
            case rtc::PeerConnection::State::Connecting: value = PeerState::Connecting; break;
            case rtc::PeerConnection::State::Connected: value = PeerState::Connected; break;
            case rtc::PeerConnection::State::Disconnected: value = PeerState::Disconnected; break;
            case rtc::PeerConnection::State::Failed: value = PeerState::Failed; break;
            case rtc::PeerConnection::State::Closed: value = PeerState::Closed; break;
            }
            if (stateCallback_) stateCallback_(value);
        }));
        pc_->onGatheringStateChange(guarded(weak, [this](rtc::PeerConnection::GatheringState s) {
            gatheringComplete_ = s == rtc::PeerConnection::GatheringState::Complete;
        }));
        h264Depacketizer_.SetCallback([this](const uint8_t* data, size_t size, uint32_t ticks) {
            if (dataChannelVideo_) return;
            if (km::BoundedVideoQueue::configuredIdr({data, size})) needKeyframe_ = false;
            const auto timestampUs = km::TicksToMicroseconds(rtpTime_.unwrap(ticks), 90000);
            if (videoCallback_) videoCallback_(data, size, 1280, 720, timestampUs);
        });
        h264Depacketizer_.SetKeyframeRequestCallback([this] {
            bandwidthEstimator_->OnLossEventDetected(); needKeyframe_ = true; SendKeyframeRequest();
        });
        dcVideoDepacketizer_.SetCallback([this](const uint8_t* data, size_t size, int64_t raw) {
            dataChannelVideo_ = true;
            if (km::BoundedVideoQueue::configuredIdr({data, size})) needKeyframe_ = false;
            const auto timestampUs = dcTime_.unwrap(uint32_t(raw));
            if (videoCallback_) videoCallback_(data, size, 1280, 720, timestampUs);
        });
        dcVideoDepacketizer_.SetControlSendCallback([this](const std::string& text) {
            if (controlDc_ && controlDc_->isOpen()) controlDc_->send(text);
        });
        pc_->onDataChannel(guarded(weak, [this, weak](std::shared_ptr<rtc::DataChannel> channel) {
            std::lock_guard lock(rtcMutex_);
            if (channel->label() == km::dc_protocol::kDataChannelVideo) {
                if (videoDc_) { channel->close(); std::cerr << "[RTC] video datachannel rejected: duplicate\n"; return; }
                videoDc_ = channel;
                std::cout << "[RTC] datachannel open: video\n";
                channel->onMessage(guarded(weak, [this](rtc::message_variant message) {
                    std::lock_guard lock(rtcMutex_);
                    if (auto* bytes = std::get_if<rtc::binary>(&message)) dcVideoDepacketizer_.ProcessDataChannelPacket(reinterpret_cast<const uint8_t*>(bytes->data()), bytes->size());
                }));
                channel->onClosed(guarded(weak, [this] {
                    std::lock_guard lock(rtcMutex_); dataChannelVideo_ = false; videoDc_.reset(); dcVideoDepacketizer_.Reset(); dcTime_.reset();
                    h264Depacketizer_.Reset(); rtpTime_.reset(); needKeyframe_ = true;
                }));
            } else if (channel->label() == km::dc_protocol::kDataChannelControl) {
                if (controlDc_) { channel->close(); std::cerr << "[RTC] control datachannel rejected: duplicate\n"; return; }
                controlDc_ = channel;
                std::cout << "[RTC] datachannel open: control\n";
                channel->onMessage(guarded(weak, [this](rtc::message_variant message) {
                    std::lock_guard lock(rtcMutex_);
                    auto* text = std::get_if<std::string>(&message); if (!text || text->size() > 65536) return;
                    const auto root = km::json::parse(*text);
                    if (const auto* type = root.find("type"); type && type->string() == "ping" && controlDc_ && controlDc_->isOpen()) controlDc_->send("{\"type\":\"pong\"}");
                    if (controlCallback_) controlCallback_(*text);
                }));
            } else { channel->close(); std::cerr << "[RTC] datachannel rejected: " << channel->label() << "\n"; }
        }));
        pc_->onTrack(guarded(weak, [this, weak](std::shared_ptr<rtc::Track> track) {
            std::lock_guard lock(rtcMutex_); auto media = track->description();
            if (allTracks_.size() >= 2) { track->close(); std::cerr << "[RTC] track rejected: at track limit type=" << media.type() << "\n"; return; }
            if (media.type() == "video") {
                if (videoTrack_) { track->close(); std::cerr << "[RTC] video track rejected: duplicate\n"; return; }
                std::vector<uint8_t> payloadTypes;
                for (int pt : media.payloadTypes()) {
                    const auto* map = media.rtpMap(pt);
                    if (map && lower(map->format) == "h264" && map->clockRate == 90000 && pt >= 0 && pt <= 127) payloadTypes.push_back(uint8_t(pt));
                }
                if (payloadTypes.empty()) { track->close(); std::cerr << "[RTC] video track rejected: no H264 payload type\n"; return; }
                videoTrack_ = track; videoRtcpSession_ = std::make_shared<EnhancedRtcpReceivingSession>();
                videoRtcpSession_->SetBandwidthEstimator(bandwidthEstimator_);
                for (int id : media.extIds()) {
                    auto* ext = media.extMap(id);
                    if (id > 0 && id <= 14 && ext && ext->uri.find("transport-wide-cc") != std::string::npos) videoRtcpSession_->SetTransportCcExtensionId(uint8_t(id));
                }
                track->setMediaHandler(videoRtcpSession_);
                std::cout << "[RTC] video track accepted h264 payloadTypes=" << payloadTypes.size() << "\n";
                track->onMessage(guarded(weak, [this, payloadTypes](rtc::message_variant message) {
                    std::lock_guard lock(rtcMutex_);
                    auto* bytes = std::get_if<rtc::binary>(&message); if (!bytes) return;
                    const km::wire::Bytes wire{reinterpret_cast<const uint8_t*>(bytes->data()), bytes->size()};
                    const auto packet = km::wire::parseRtp(wire);
                    if (!packet || std::find(payloadTypes.begin(), payloadTypes.end(), packet->payloadType) == payloadTypes.end()) return;
                    if (!haveVideoSsrc_ || videoSsrc_ != packet->ssrc) { rtpTime_.reset(); videoSsrc_ = packet->ssrc; haveVideoSsrc_ = true; }
                    const auto now = nowUs() / 1000;
                    bandwidthEstimator_->OnRtpPacketReceived(bytes->size(), packet->sequence, now);
                    if (!dataChannelVideo_) h264Depacketizer_.ProcessRtpPacket(wire.data(), wire.size());
                    uint32_t target = 0;
                    if (bandwidthEstimator_->EvaluateEstimation(now, target) && videoTrack_) videoTrack_->requestBitrate(target);
                }));
            } else if (media.type() == "audio") {
                if (audioTrack_) { track->close(); return; }
                int opusPt = -1;
                for (int pt : media.payloadTypes()) {
                    const auto* map = media.rtpMap(pt);
                    if (map && lower(map->format) == "opus" && map->clockRate == 48000 && pt >= 0 && pt <= 127) { opusPt = pt; break; }
                }
                if (opusPt < 0 || !opusDecoder_.Configure(uint8_t(opusPt), [this](const int16_t* pcm, size_t elements, int channels, int rate) {
                    if (audioCallback_) audioCallback_(pcm, elements, channels, rate);
                })) { std::cerr << "[RTC] Opus audio unavailable opusPt=" << opusPt << "; audio track rejected\n"; track->close(); return; }
                audioTrack_ = track; audioRtcpSession_ = std::make_shared<EnhancedRtcpReceivingSession>(48000);
                std::cout << "[RTC] audio track accepted opusPt=" << opusPt << "\n";
                track->setMediaHandler(audioRtcpSession_);
                track->onMessage(guarded(weak, [this](rtc::message_variant message) {
                    std::lock_guard lock(rtcMutex_);
                    if (auto* bytes = std::get_if<rtc::binary>(&message)) {
                        static std::atomic<uint64_t> rtpPkts{0}, rtpBytes{0};
                        const uint64_t n = ++rtpPkts, b = (rtpBytes += bytes->size());
                        if (n <= 3 || n % 500 == 0) std::cout << "[RTC] audio rtp packets=" << n << " bytes=" << b << " size=" << bytes->size() << "\n";
                        opusDecoder_.Receive({reinterpret_cast<const uint8_t*>(bytes->data()), bytes->size()}, nowUs());
                    }
                }));
            } else { track->close(); std::cerr << "[RTC] track rejected: unsupported media type " << media.type() << "\n"; return; }
            allTracks_.push_back(track);
        }));
        timer_ = std::jthread([this, weak](std::stop_token stop) {
            while (!stop.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                auto gate = weak.lock(); if (!gate) break;
                auto lease = gate->enter(); if (!lease) break;
                try {
                    std::lock_guard lock(rtcMutex_);
                    dcVideoDepacketizer_.OnTimerTick(); opusDecoder_.Tick(nowUs());
                    // A closed/failed peer throws on every feedback flush; only touch
                    // the transport while it is actually connected.
                    const bool live = pc_ && pc_->state() == rtc::PeerConnection::State::Connected;
                    if (live && videoRtcpSession_) videoRtcpSession_->FlushFeedback();
                    if (live && audioRtcpSession_) audioRtcpSession_->FlushFeedback();
                    if (live && needKeyframe_) SendKeyframeRequest();
                } catch (const std::exception&) { std::cerr << "[RTC] Timer processing failed\n"; }
            }
        });
        std::cout << "[RTC] initialize ok icePolicy=" << config.iceTransportPolicy
                  << " iceServers=" << native.iceServers.size() << " usableTurn=" << (usableTurn ? 1 : 0) << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[RTC] Initialize failed: " << e.what() << "\n";
        CloseInternal();
        return false;
    }
}
bool PeerConnectionManager::ProcessOfferAndGenerateAnswer(const std::string& sdp, std::string& answer) {
    answer.clear(); std::shared_ptr<rtc::PeerConnection> peer; uint64_t serial = 0;
    { std::lock_guard lock(rtcMutex_); peer = pc_; serial = sessionSerial_.load(); gatheringComplete_ = false; }
    if (!peer || cancelled_ || sdp.empty() || sdp.size() > 512 * 1024) {
        std::cerr << "[RTC] answer generation rejected: peer=" << (peer ? "set" : "none")
                  << " cancelled=" << (cancelled_ ? "yes" : "no") << " offerBytes=" << sdp.size() << "\n";
        return false;
    }
    try {
        rtc::Description offer(sdp, rtc::Description::Type::Offer, rtc::Description::Role::Active);
        for (int i = 0; i < offer.mediaCount(); ++i) {
            auto entry = offer.media(i);
            if (auto* m = std::get_if<rtc::Description::Media*>(&entry)) {
                auto* media = *m;
                // First decide per m-line: reject it whole, or keep it and drop
                // the unwanted codecs. A rejected m-line must keep its payload
                // types - the answer's format list derives from them and an
                // empty list makes browsers reject the entire answer.
                bool keepAny = false;
                for (int pt : media->payloadTypes()) {
                    if (!media->hasPayloadType(pt)) continue;
                    const auto* map = media->rtpMap(pt);
                    keepAny = keepAny || isAnsweredCodec(*media, map);
                }
                if (!keepAny) {
                    media->markRemoved();
                    std::cout << "[RTC] offer m-line rejected: " << media->type() << "\n";
                    continue;
                }
                for (int pt : media->payloadTypes()) {
                    // removeRtpMap cascades into dependent entries (RTX apt=...);
                    // a payload type the cascade already dropped is skipped.
                    if (!media->hasPayloadType(pt)) continue;
                    const auto* map = media->rtpMap(pt);
                    if (map && !isAnsweredCodec(*media, map)) media->removeRtpMap(pt);
                }
            }
        }
        std::cout << "[RTC] offer accepted bytes=" << sdp.size() << " mediaCount=" << offer.mediaCount() << "\n";
        peer->setRemoteDescription(offer);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!gatheringComplete_ && peer->gatheringState() != rtc::PeerConnection::GatheringState::Complete) {
            if (cancelled_ || serial != sessionSerial_) {
                std::cerr << "[RTC] answer generation aborted: session changed\n";
                return false;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                std::cerr << "[RTC] answer generation timeout: ICE gathering\n";
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (cancelled_ || serial != sessionSerial_) {
            std::cerr << "[RTC] answer generation aborted: session changed\n";
            return false;
        }
        auto local = peer->localDescription();
        if (!local) {
            std::cerr << "[RTC] answer generation failed: no local description\n";
            return false;
        }
        answer = std::string(*local);
        if (answer.empty()) {
            std::cerr << "[RTC] answer generation failed: empty local description\n";
            return false;
        }
        std::cout << "[RTC] answer generated bytes=" << answer.size() << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[RTC] answer generation exception: " << e.what() << "\n";
        return false;
    }
}
void PeerConnectionManager::SendKeyframeRequest() {
    const auto now = nowUs(); if (lastKeyframeRequestUs_ >= 0 && now - lastKeyframeRequestUs_ < 50000) return;
    lastKeyframeRequestUs_ = now;
    static std::atomic<int64_t> lastLogUs{-1000000000LL};
    if (now - lastLogUs.exchange(now) > 5000000) std::cout << "[RTC] keyframe requested\n";
    try {
        if (dataChannelVideo_ || videoDc_) { if (controlDc_ && controlDc_->isOpen()) controlDc_->send("{\"type\":\"pli\"}"); }
        else if (videoRtcpSession_) videoRtcpSession_->RequestKeyframeDirect();
    } catch (const std::exception&) { /* timer retries until a configured IDR arrives */ }
}
void PeerConnectionManager::RequestKeyframe() { std::lock_guard lock(rtcMutex_); needKeyframe_ = true; SendKeyframeRequest(); }
void PeerConnectionManager::RequestBitrate(uint32_t bitrate) { std::lock_guard lock(rtcMutex_); if (videoTrack_) videoTrack_->requestBitrate(bitrate); }
uint32_t PeerConnectionManager::GetEstimatedBitrate() const { std::lock_guard lock(rtcMutex_); return bandwidthEstimator_->GetCurrentEstimatedBitrate(); }
uint32_t PeerConnectionManager::GetMeasuredThroughput() const { std::lock_guard lock(rtcMutex_); return bandwidthEstimator_->GetMeasuredThroughputBps(); }
float PeerConnectionManager::GetLossRatio() const { std::lock_guard lock(rtcMutex_); return bandwidthEstimator_->GetCurrentLossRatio(); }
void PeerConnectionManager::SendControlMessage(const std::string& text) {
    if (text.size() > 65536) return; std::lock_guard lock(rtcMutex_);
    try { if (controlDc_ && controlDc_->isOpen()) controlDc_->send(text); } catch (const std::exception&) {}
}
void PeerConnectionManager::SetControlMessageCallback(std::function<void(const std::string&)> callback) { std::lock_guard lock(rtcMutex_); controlCallback_ = std::move(callback); }
void PeerConnectionManager::CancelPending() { cancelled_ = true; }
void PeerConnectionManager::Close() { std::lock_guard lifecycle(lifecycleMutex_); CloseInternal(); }
void PeerConnectionManager::CloseInternal() {
    cancelled_ = true; ++sessionSerial_;
    if (gate_) {
        std::cout << "[RTC] close stage gate-wait-begin\n";
        gate_->closeAndWait(); // no rtcMutex held while waiting for callbacks
        std::cout << "[RTC] close stage gate-waited\n";
    }
    if (timer_.joinable()) { timer_.request_stop(); timer_.join(); }
    std::cout << "[RTC] close stage timer-joined\n";
    std::shared_ptr<rtc::PeerConnection> peer;
    {
        std::lock_guard lock(rtcMutex_);
        if (videoRtcpSession_) videoRtcpSession_->Stop();
        if (audioRtcpSession_) audioRtcpSession_->Stop();
        peer = std::move(pc_); videoDc_.reset(); controlDc_.reset(); allTracks_.clear();
        videoTrack_.reset(); audioTrack_.reset(); videoRtcpSession_.reset(); audioRtcpSession_.reset();
        h264Depacketizer_.Reset(); dcVideoDepacketizer_.Reset(); opusDecoder_.Reset(); rtpTime_.reset(); dcTime_.reset();
        dataChannelVideo_ = haveVideoSsrc_ = false; needKeyframe_ = true; lastKeyframeRequestUs_ = -1;
        stateCallback_ = {}; videoCallback_ = {}; audioCallback_ = {}; controlCallback_ = {}; gate_.reset();
    }
    if (peer) {
        std::cout << "[RTC] close stage peer-begin\n";
        try { peer->close(); } catch (const std::exception&) {}
        std::cout << "[RTC] close stage peer-done\n";
    }
}
} // namespace km::rtc_net
