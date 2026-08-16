#include <iostream>
#include <cassert>
#include <chrono>
#include <thread>
#include <atomic>
#include <rtc/rtc.hpp>
#include "../receiver/signaling/signaling_models.h"
#include "../receiver/rtc/peer_connection_manager.h"

void TestWebRtcDtlsHandshake() {
    std::cout << "[TEST] Running TestWebRtcDtlsHandshake (libdatachannel loopback DTLS test)..." << std::endl;

    km::signaling::RtcConfiguration config;
    km::signaling::IceServer ice;
    ice.urls.push_back("stun:stun.l.google.com:19302");
    config.iceServers.push_back(ice);

    km::rtc_net::PeerConnectionManager receiverManager;
    std::atomic<bool> receiverConnected{false};
    std::atomic<bool> frameReceived{false};

    bool ok = receiverManager.Initialize(
        config,
        [&receiverConnected](km::rtc_net::PeerState state) {
            if (state == km::rtc_net::PeerState::Connected) {
                receiverConnected = true;
            }
        },
        [&frameReceived](const uint8_t* data, size_t size, int width, int height, int64_t tsUs) {
            if (size > 0) {
                frameReceived = true;
            }
        },
        nullptr
    );
    assert(ok && "Failed to initialize receiverManager");

    // Create client PeerConnection (simulating Browser / Safari)
    rtc::Configuration clientConfig;
    rtc::IceServer server("stun:stun.l.google.com:19302");
    clientConfig.iceServers.push_back(server);

    auto clientPc = std::make_shared<rtc::PeerConnection>(clientConfig);
    std::atomic<bool> clientConnected{false};

    clientPc->onStateChange([&clientConnected](rtc::PeerConnection::State state) {
        if (state == rtc::PeerConnection::State::Connected) {
            clientConnected = true;
        }
    });

    // Add video track
    rtc::Description::Video videoMedia("video", rtc::Description::Direction::SendOnly);
    videoMedia.addH264Codec(96);
    videoMedia.addSSRC(12345, "cname-test");
    auto clientTrack = clientPc->addTrack(videoMedia);

    // Wait for client ICE gathering
    std::atomic<bool> clientGatheringDone{false};
    clientPc->onGatheringStateChange([&clientGatheringDone](rtc::PeerConnection::GatheringState state) {
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            clientGatheringDone = true;
        }
    });

    clientPc->setLocalDescription();

    auto start = std::chrono::steady_clock::now();
    while (!clientGatheringDone) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        if (elapsed > 6000) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    auto clientOfferOpt = clientPc->localDescription();
    assert(clientOfferOpt.has_value() && "Client failed to create local description");
    std::string clientOfferSdp = std::string(clientOfferOpt.value());

    // Receiver processes client offer and creates answer
    std::string receiverAnswerSdp;
    bool answerOk = receiverManager.ProcessOfferAndGenerateAnswer(clientOfferSdp, receiverAnswerSdp);
    assert(answerOk && "Receiver failed to generate answer");
    assert(!receiverAnswerSdp.empty() && "Answer SDP is empty");
    assert(receiverAnswerSdp.find("a=setup:passive") != std::string::npos && "Answer SDP must specify a=setup:passive");

    // Client applies receiver answer
    clientPc->setRemoteDescription(rtc::Description(receiverAnswerSdp, "answer"));

    // Wait for DTLS handshake to connect
    start = std::chrono::steady_clock::now();
    while (!receiverConnected || !clientConnected) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        if (elapsed > 10000) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "  Receiver connected: " << (receiverConnected ? "YES" : "NO") << std::endl;
    std::cout << "  Client connected: " << (clientConnected ? "YES" : "NO") << std::endl;
    assert(receiverConnected && "Receiver failed DTLS connection");
    assert(clientConnected && "Client failed DTLS connection");

    std::cout << "[PASS] TestWebRtcDtlsHandshake passed completely (100% DTLS handshake & role verified)." << std::endl;
}

int main() {
    TestWebRtcDtlsHandshake();
    return 0;
}
