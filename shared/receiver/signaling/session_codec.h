#pragma once
#include "signaling_models.h"
#include "../../km/json.h"
#include <optional>
#include <stdexcept>
#include <string_view>
namespace km::signaling {
inline bool safeHeader(std::string_view s) {
    for (unsigned char c : s) if (c < 32 || c == 127) return false;
    return true;
}
inline bool safeSessionId(std::string_view s) {
    if (s.empty() || s.size() > 128) return false;
    for (char c : s) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}
inline bool supportedIceUrl(std::string_view s) {
    return s.size() <= 2048 && safeHeader(s) && s.find(' ') == std::string_view::npos &&
        (s.starts_with("stun:") || s.starts_with("turn:") || s.starts_with("turns:"));
}
inline CreateSessionResponse decodeSession(std::string_view body) {
    const auto root = json::parse(body); CreateSessionResponse out;
    out.sessionId = root.at("sessionId").string(); out.receiverToken = root.at("receiverToken").string();
    out.joinUrl = root.at("joinUrl").string(); out.expiresAt = root.at("expiresAt").string();
    if (!safeSessionId(out.sessionId) || out.receiverToken.empty() || out.receiverToken.size() > 8192 ||
        !safeHeader(out.receiverToken) || !safeHeader(out.joinUrl) || out.joinUrl.size() > 8192 ||
        !(out.joinUrl.starts_with("https://") || out.joinUrl.starts_with("http://")))
        throw std::runtime_error("invalid session identity or URL");
    const auto& poll = root.at("poll");
    out.poll.initialIntervalMs = poll.at("initialIntervalMs").uint32();
    out.poll.backoffAfterMs = poll.at("backoffAfterMs").uint32();
    out.poll.maxIntervalMs = poll.at("maxIntervalMs").uint32();
    out.poll.timeoutMs = poll.at("timeoutMs").uint32();
    if (out.poll.initialIntervalMs < 20 || out.poll.maxIntervalMs < out.poll.initialIntervalMs ||
        out.poll.maxIntervalMs > 60000 || out.poll.timeoutMs < out.poll.maxIntervalMs ||
        out.poll.timeoutMs > 3600000 || out.poll.backoffAfterMs > out.poll.timeoutMs)
        throw std::runtime_error("invalid poll policy");
    const auto& config = root.at("rtcConfiguration");
    if (const auto* policy = config.find("iceTransportPolicy")) out.rtcConfiguration.iceTransportPolicy = policy->string();
    if (out.rtcConfiguration.iceTransportPolicy != "all" && out.rtcConfiguration.iceTransportPolicy != "relay")
        throw std::runtime_error("invalid ICE transport policy");
    const auto& servers = config.at("iceServers");
    if (servers.kind != json::Value::Kind::Array || servers.array.size() > 32) throw std::runtime_error("invalid ICE server list");
    size_t urlCount = 0;
    for (const auto& item : servers.array) {
        IceServer server; const auto& urls = item.at("urls");
        if (urls.kind == json::Value::Kind::String) server.urls.push_back(urls.string());
        else if (urls.kind == json::Value::Kind::Array) for (const auto& url : urls.array) server.urls.push_back(url.string());
        else throw std::runtime_error("invalid ICE URLs");
        if (server.urls.empty() || (urlCount += server.urls.size()) > 64) throw std::runtime_error("too many ICE URLs");
        if (const auto* user = item.find("username")) server.username = user->string();
        if (const auto* password = item.find("credential")) server.credential = password->string();
        if (server.username.size() > 4096 || server.credential.size() > 4096 || !safeHeader(server.username) || !safeHeader(server.credential))
            throw std::runtime_error("invalid ICE credentials");
        if (const auto* kind = item.find("credentialType"); kind && kind->string() != "password") throw std::runtime_error("unsupported ICE credential type");
        for (const auto& url : server.urls) {
            if (!supportedIceUrl(url)) throw std::runtime_error("unsupported ICE URL");
            if ((url.starts_with("turn:") || url.starts_with("turns:")) && (server.username.empty() || server.credential.empty()))
                throw std::runtime_error("missing TURN credentials");
        }
        out.rtcConfiguration.iceServers.push_back(std::move(server));
    }
    return out;
}
inline OfferDescription decodeOffer(std::string_view body) {
    const auto root = json::parse(body); OfferDescription offer;
    offer.type = root.at("type").string(); offer.sdp = root.at("sdp").string();
    if (offer.type != "offer" || offer.sdp.empty() || offer.sdp.size() > 512 * 1024 || offer.sdp.find('\0') != std::string::npos)
        throw std::runtime_error("invalid SDP offer");
    return offer;
}
} // namespace km::signaling
