// Offline load smoke for the RTC integration (libdatachannel adoption).
// Builds PeerConnectionManager against libdatachannel and checks configuration
// gating WITHOUT any connection attempt: Initialize only validates the config
// and constructs the PeerConnection object; offer/answer processing and ICE
// gathering are never started, so no socket leaves this process's own state.
#include "peer_connection_manager.h"

#include <iostream>
#include <string>

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ":" << __LINE__ << ": " #x "\n"; return 1; } } while (false)

namespace {
using km::rtc_net::PeerConnectionManager;
using km::rtc_net::PeerState;
using km::signaling::IceServer;
using km::signaling::RtcConfiguration;

bool initialize(PeerConnectionManager& mgr, const RtcConfiguration& config) {
    return mgr.Initialize(config,
        [](PeerState) {},
        [](const uint8_t*, size_t, int, int, int64_t) {},
        [](const int16_t*, size_t, int, int) {});
}

RtcConfiguration relayWith(const std::string& url, const std::string& user, const std::string& pass) {
    RtcConfiguration config;
    config.iceTransportPolicy = "relay";
    IceServer server;
    server.urls.push_back(url);
    server.username = user;
    server.credential = pass;
    config.iceServers.push_back(std::move(server));
    return config;
}
} // namespace

int main() {
    { // Default policy with no servers: construct and tear down offline.
        PeerConnectionManager mgr;
        CHECK(initialize(mgr, RtcConfiguration{}));
        mgr.Close();
    }
    { // relay policy with no TURN endpoint at all must be refused.
        RtcConfiguration config;
        config.iceTransportPolicy = "relay";
        PeerConnectionManager mgr;
        CHECK(!initialize(mgr, config));
    }
    { // UDP TURN (libjuice default) is accepted; loopback endpoint, no traffic.
        PeerConnectionManager mgr;
        CHECK(initialize(mgr, relayWith("turn:127.0.0.1:3478", "u", "p")));
        mgr.Close();
    }
    { // TURN over TLS is not carried in a libjuice build (KM_TURN_TCP_TLS=0):
        // the endpoint is skipped, so relay-only fails instead of pretending.
        PeerConnectionManager mgr;
        CHECK(!initialize(mgr, relayWith("turns:127.0.0.1:5349", "u", "p")));
    }
    { // TURN URL without credentials is refused before any network use.
        PeerConnectionManager mgr;
        CHECK(!initialize(mgr, relayWith("turn:127.0.0.1:3478", "", "")));
    }
    { // Unsupported scheme is refused at the shared supportedIceUrl gate.
        PeerConnectionManager mgr;
        CHECK(!initialize(mgr, relayWith("http://127.0.0.1:3478", "u", "p")));
    }
    std::cout << "rtc_load ok\n";
    return 0;
}
