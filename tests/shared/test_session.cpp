#include "receiver/signaling/session_client.h"
#include <iostream>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; return 1; } } while (false)
struct Mock : km::IHttpTransport {
    km::HttpRequest last; km::HttpResponse next; bool throwing=false; int calls=0;
    km::HttpResponse perform(const km::HttpRequest& r) override { ++calls; last=r; if(throwing) throw std::runtime_error("cancelled"); return next; }
    void cancel() override { throwing=true; }
};
int main() {
    Mock mock; km::signaling::SessionClient client(mock,"https://example.test/prefix/");
    mock.next.status=201;
    mock.next.body=R"({"sessionId":"abc-123","receiverToken":"secret","joinUrl":"https://example.test/send/abc","expiresAt":"2026-09-23","poll":{"initialIntervalMs":1000,"backoffAfterMs":15000,"maxIntervalMs":2000,"timeoutMs":60000},"rtcConfiguration":{"iceTransportPolicy":"relay","iceServers":[{"urls":["turn:[2001:db8::1]:3478?transport=udp","turns:relay.example:5349"],"username":"user","credential":"password"}]}})";
    auto session=client.CreateSession("receiver\"\n"); CHECK(session); CHECK(session->rtcConfiguration.iceTransportPolicy=="relay");
    CHECK(session->rtcConfiguration.iceServers[0].urls.size()==2); CHECK(session->rtcConfiguration.iceServers[0].credential=="password");
    CHECK(mock.last.url=="https://example.test/prefix/v1/sessions");
    CHECK(km::json::parse(mock.last.body).find("client")->find("name")->string()=="receiver\"\n");
    mock.next.status=200;mock.next.body="{\"type\":\"offer\",\"sdp\":\"v=0\\r\\n\"}";
    CHECK(client.PollOffer("abc-123","secret")->sdp=="v=0\r\n");
    mock.next.status=204;mock.next.body.clear();CHECK(client.PutAnswer("abc-123","secret","v=0\r\n"));
    CHECK(km::json::parse(mock.last.body).find("sdp")->string()=="v=0\r\n");
    const int before=mock.calls; CHECK(!client.PollOffer("../other","secret")); CHECK(mock.calls==before);
    CHECK(!client.PutAnswer("abc-123","secret\r\nX: injected","v=0"));CHECK(mock.calls==before);
    mock.next.status=200;mock.next.body.assign(1024*1024+1,' ');CHECK(!client.PollOffer("abc-123","secret"));
    CHECK(client.LastError()=="response exceeds byte limit");
    mock.cancel();CHECK(!client.PutAnswer("abc-123","secret","v=0"));CHECK(!client.DeleteSession("abc-123","secret"));
    std::cout<<"session_protocol: typed JSON, TURN preservation, escaping, response bounds and exceptions passed\n";
}
