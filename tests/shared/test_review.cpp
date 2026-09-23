#include "km/rtp_wire.h"
#include "km/bounded_video_queue.h"
#include "km/callback_gate.h"
#include "receiver/codec/h264_rtp_depacketizer.h"
#include "receiver/signaling/session_codec.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << ": " #x "\n"; return 1; } } while (false)
using Bytes = std::vector<uint8_t>;
Bytes rtp(uint16_t seq, uint32_t ts, bool marker, Bytes payload) {
    Bytes out{0x80,uint8_t(96|(marker?128:0)),uint8_t(seq>>8),uint8_t(seq),uint8_t(ts>>24),uint8_t(ts>>16),uint8_t(ts>>8),uint8_t(ts),0,0,0,1};
    out.insert(out.end(),payload.begin(),payload.end()); return out;
}
int main() {
    const Bytes config{0x78,0,3,0x67,0x42,0x80,0,2,0x68,0xce};
    km::codec::H264RtpDepacketizer d; std::vector<Bytes> frames; int requests=0;
    d.SetCallback([&](const uint8_t* p,size_t n,uint32_t){frames.emplace_back(p,p+n);});
    d.SetKeyframeRequestCallback([&]{++requests;});
    auto feed=[&](Bytes p){d.ProcessRtpPacket(p.data(),p.size());};
    feed(rtp(0,0,false,config)); feed(rtp(1,0,true,{0x65,1})); CHECK(frames.size()==1);
    CHECK(km::BoundedVideoQueue::configuredIdr(frames[0]));
    // Reject duplicate/stale payload completely (not just its sequence update).
    feed(rtp(1,0,true,{0x65,2}));feed(rtp(0,999,true,{0x41,3})); CHECK(frames.size()==1);
    feed(rtp(2,3000,true,{0x41,4})); CHECK(frames.size()==2);
    // A missing FU tail cannot escape when the timestamp advances.
    feed(rtp(3,6000,false,{0x7c,0x81,0x55})); feed(rtp(4,9000,true,{0x41,4}));CHECK(frames.size()==2 && requests>0);
    feed(rtp(5,12000,true,{0x65,5}));CHECK(frames.size()==3);
    // STAP-A zero-length NAL is an invalid packet even when no byte follows.
    feed(rtp(6,15000,true,{0x78,0,0}));CHECK(frames.size()==3);
    feed(rtp(7,18000,true,{0x41,4}));CHECK(frames.size()==3);
    feed(rtp(8,21000,true,{0x65,5}));CHECK(frames.size()==4);
    // Gap => full dependency-chain invalidation, including a partial current AU.
    feed(rtp(10,24000,true,{0x41,4}));feed(rtp(11,27000,true,{0x41,4}));CHECK(frames.size()==4);
    feed(rtp(12,30000,true,{0x65,5}));CHECK(frames.size()==5);
    // Changed SSRC cannot reuse parameter sets belonging to another source.
    auto switched=rtp(0,0,true,{0x65,6});switched[11]=2;feed(switched);CHECK(frames.size()==5);
    d.Reset();frames.clear();feed(rtp(65534,0,false,config));feed(rtp(65535,0,true,{0x65,7}));feed(rtp(0,3000,true,{0x41,8}));CHECK(frames.size()==2);
    // RTP extension and padding boundaries.
    auto bad=rtp(1,1,true,{0x65,1});bad[0]|=32;bad.back()=0;CHECK(!km::wire::parseRtp(bad));
    bad.back()=255;CHECK(!km::wire::parseRtp(bad));bad[0]=0x90;CHECK(!km::wire::parseRtp(bad));
    auto valid=rtp(1,1,true,{0x65,0,0,0,3});valid[0]|=32;CHECK(km::wire::parseRtp(valid)->payload.size()==2);
    // SR/RR report count and compound validation are all-or-nothing.
    std::vector<km::wire::RtcpPacket> reports;
    Bytes sr(28);sr[0]=0x80;sr[1]=200;sr[3]=6;CHECK(km::wire::parseRtcp(sr,reports));
    sr[0]=0x81;CHECK(!km::wire::parseRtcp(sr,reports));sr[0]=0x80;
    Bytes rr{0x80,201,0,1,0,0,0,1};Bytes compound=sr;compound.insert(compound.end(),rr.begin(),rr.end());
    CHECK(km::wire::parseRtcp(compound,reports)&&reports.size()==2);
    compound.push_back(0);CHECK(!km::wire::parseRtcp(compound,reports)&&reports.empty());
    rr[0]=0x81;CHECK(!km::wire::parseRtcp(rr,reports));
    Bytes shortSr{0x80,200,0,0};CHECK(!km::wire::parseRtcp(shortSr,reports));
    // Configured-IDR gating, byte/frame caps, generation and overflow serial.
    km::BoundedVideoQueue q;const auto generation=q.reset();const Bytes p{0,0,1,0x41,1};
    const Bytes idr{0,0,1,0x67,0x42,0,0,1,0x68,0xce,0,0,1,0x65,1};
    CHECK(!q.push(p,0,generation));CHECK(q.push(idr,0,generation));
    km::QueuedAccessUnit frame;CHECK(q.pop(frame));CHECK(q.current(frame));
    for(size_t i=0;i<q.kMaxFrames;++i) CHECK(q.push(p,int64_t(i),generation));
    CHECK(!q.push(p,10,generation));CHECK(!q.current(frame));CHECK(q.queuedBytes()==0);
    CHECK(!q.push(p,11,generation));CHECK(q.push(idr,12,generation));
    q.reset();CHECK(!q.pop(frame));CHECK(!q.push(idr,13,generation));
    // Bounded JSON grammar: correct Unicode, escaping, duplicate keys, no partial parse.
    CHECK(km::json::parse(" {\"s\":\"\\ud83d\\udc31\\n\",\"n\":42} ").at("n").uint32()==42);
    CHECK(km::json::parse(km::json::quote("a\n\t\"\\日本語")).string()=="a\n\t\"\\日本語");
    for(const auto& text: {"{\"a\":1,\"a\":2}","[1,]","01","[true false]","\"\\ud800\"","\"\\udc00\"","{\"a\":1}x","1e+","--1"}) {
        bool rejected=false;try{km::json::parse(text);}catch(const std::exception&){rejected=true;}CHECK(rejected);
    }
    bool rejected=false;try{km::json::parse(std::string("\"\xc0\x80\"",4));}catch(const std::exception&){rejected=true;}CHECK(rejected);
    const std::string session=R"({"sessionId":"a-b_c","receiverToken":"secret","joinUrl":"https://example.test/send/a","expiresAt":"2026-09-23T00:00:00Z","poll":{"initialIntervalMs":500,"backoffAfterMs":15000,"maxIntervalMs":2000,"timeoutMs":60000},"rtcConfiguration":{"iceTransportPolicy":"relay","iceServers":[{"urls":["turn:[2001:db8::1]:3478?transport=udp","turns:turn.example.test:5349?transport=tcp"],"username":"u","credential":"p"}]}})";
    auto parsed=km::signaling::decodeSession(session);CHECK(parsed.rtcConfiguration.iceServers[0].urls.size()==2);
    CHECK(parsed.rtcConfiguration.iceServers[0].credential=="p" && parsed.rtcConfiguration.iceTransportPolicy=="relay");
    CHECK(km::signaling::decodeOffer("{\"sdp\":\"v=0\\r\\n\",\"type\":\"offer\"}").sdp=="v=0\r\n");
    CHECK(!km::signaling::safeSessionId("../a"));CHECK(!km::signaling::safeHeader("x\r\nAuthorization: evil"));
    // Callback lifetime barrier: close waits for an in-flight lease; stale leases fail.
    auto gate=std::make_shared<km::CallbackGate>();std::atomic<bool> entered=false,release=false,closed=false;
    std::thread callback([&]{auto lease=gate->enter();entered=true;while(!release.load())std::this_thread::yield();});
    while(!entered.load())std::this_thread::yield();
    std::thread closer([&]{gate->closeAndWait();closed=true;});
    std::this_thread::sleep_for(std::chrono::milliseconds(5));CHECK(!closed.load());release=true;callback.join();closer.join();CHECK(!gate->enter());
    // Repeat bounded malformed RTP/RTCP/JSON mutations under sanitizers.
    std::mt19937 rng(0x52455631);
    for(int i=0;i<50000;++i) {
        auto packet=rtp(uint16_t(rng()),rng(),true,config);
        for(int j=0;j<4;++j)packet[rng()%packet.size()]=uint8_t(rng());
        if(i%3==0)packet.resize(rng()%packet.size());
        feed(packet);km::wire::parseRtcp(packet,reports);
        if(i%8==0) { try{km::json::parse(std::string(packet.begin(),packet.end()));}catch(const std::exception&){} }
        d.Reset();frames.clear();
    }
    std::cout<<"review_regressions passed; malformed datagrams=50000\n";
}
