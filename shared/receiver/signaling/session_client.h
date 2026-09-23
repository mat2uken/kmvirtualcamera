#pragma once
#include "session_codec.h"
#include "../../km/receiver_contracts.h"
namespace km::signaling {
// Protocol and JSON are OS-independent. The adapter owns TLS, cancellation and
// byte/deadline limits. Invoke request methods on one signaling worker only.
class SessionClient {
public:
    SessionClient(km::IHttpTransport& transport, std::string baseUrl) : transport_(transport), base_(std::move(baseUrl)) {
        while (!base_.empty() && base_.back() == '/') base_.pop_back();
        if (base_.empty() || base_.find_first_of("?#\r\n") != std::string::npos) throw std::invalid_argument("invalid signaling base URL");
    }
    std::optional<CreateSessionResponse> CreateSession(const std::string& name = "receiver") {
        try {
            auto r = request("POST", "/v1/sessions", "", "{\"client\":{\"name\":" + json::quote(name) + ",\"version\":\"0.2.0\"}}");
            if (r.status != 201) return {};
            return decodeSession(r.body);
        } catch (const std::exception& e) { error_ = e.what(); return {}; }
    }
    std::optional<OfferDescription> PollOffer(const std::string& id, const std::string& token) {
        if (!safeSessionId(id)) { error_ = "invalid session ID"; return {}; }
        try {
            auto r = request("GET", "/v1/sessions/" + id + "/offer", token);
            if (r.status != 200) return {};
            return decodeOffer(r.body);
        } catch (const std::exception& e) { error_ = e.what(); return {}; }
    }
    bool PutAnswer(const std::string& id, const std::string& token, const std::string& sdp) {
        if (!safeSessionId(id) || sdp.empty() || sdp.size() > 512 * 1024) return false;
        try { return request("PUT", "/v1/sessions/" + id + "/answer", token, "{\"type\":\"answer\",\"sdp\":" + json::quote(sdp) + "}").status == 204; }
        catch (const std::exception& e) { error_ = e.what(); return false; }
    }
    bool DeleteSession(const std::string& id, const std::string& token) {
        try { return safeSessionId(id) && request("DELETE", "/v1/sessions/" + id, token).status == 204; }
        catch (const std::exception& e) { error_ = e.what(); return false; }
    }
    const std::string& LastError() const { return error_; }
private:
    km::HttpResponse request(const std::string& method, const std::string& path, const std::string& token, const std::string& body = "") {
        km::HttpResponse response; error_.clear();
        if (!safeHeader(token) || token.size() > 8192) { response.error = error_ = "invalid bearer token"; return response; }
        km::HttpRequest request; request.method = method; request.url = base_ + path; request.body = body;
        request.headers.emplace_back("Accept", "application/json");
        if (!body.empty()) request.headers.emplace_back("Content-Type", "application/json");
        if (!token.empty()) request.headers.emplace_back("Authorization", "Bearer " + token);
        response = transport_.perform(request);
        // Defense in depth: an adapter must not make the parser accept oversized input.
        if (response.body.size() > request.maxResponseBytes) { response.body.clear(); response.status = 0; response.error = "response exceeds byte limit"; }
        error_ = response.error; return response;
    }
    km::IHttpTransport& transport_; std::string base_, error_;
};
} // namespace km::signaling
