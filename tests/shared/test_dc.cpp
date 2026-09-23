#include "receiver/codec/dc_video_depacketizer.h"
#include "km/h264.h"
#include <filesystem>
#include <iostream>
#include <random>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::cerr<<__FILE__<<":"<<__LINE__<<": " #x "\n";return 1;} } while(false)
using D=km::codec::DcVideoDepacketizer;
static std::vector<uint8_t> Packet(uint16_t seq,uint32_t ts,bool key,uint8_t index,uint8_t count,std::span<const uint8_t> data) {
    std::vector<uint8_t> p{0x4d,0x4b,uint8_t((key?1:0)|(index==0?4:0)|(index+1==count?8:0)),1,index,count,
        uint8_t(seq),uint8_t(seq>>8),uint8_t(ts),uint8_t(ts>>8),uint8_t(ts>>16),uint8_t(ts>>24)};
    p.insert(p.end(),data.begin(),data.end());return p;
}
int main() {
    const std::vector<uint8_t> key{0,0,0,1,0x67,0x42,0x80,0,0,1,0x68,0xce,0,0,1,0x65,0xaa};
    const std::vector<uint8_t> delta{0,0,1,0x41,0xbb};
    D d;int received=0;int64_t ts=-1;
    d.SetCallback([&](const uint8_t*,size_t,int64_t t){++received;ts=t;});
    auto first=Packet(65535,0xfffffff0,true,0,2,std::span(key).first(8));
    auto last=Packet(65535,0xfffffff0,true,1,2,std::span(key).subspan(8));
    d.ProcessPacketAt(last,0);d.ProcessPacketAt(last,1);CHECK(received==0);
    d.ProcessPacketAt(first,2);CHECK(received==1 && ts==4294967280ll);
    d.ProcessPacketAt(Packet(0,15,false,0,1,delta),3);CHECK(received==2 && ts==15);
    d.ProcessPacketAt(first,4);CHECK(received==2);CHECK(d.GetPendingBytes()==0);
    // Metadata mismatch previously allowed chunks[index] out-of-bounds.
    d.Reset();received=0;
    d.ProcessPacketAt(Packet(1,1,false,0,1,delta),0);
    d.ProcessPacketAt(Packet(1,1,false,1,2,delta),1);
    CHECK(d.GetPendingFrames()==0 && received==0);
    // Expiration must run before an initial keyframe has arrived.
    d.ProcessPacketAt(Packet(2,2,false,0,1,delta),2);CHECK(d.GetPendingFrames()==1);
    d.OnTimerTickAt(70000);CHECK(d.GetPendingFrames()==0 && d.GetPendingBytes()==0);
    // Cap pre-keyframe state, including entirely missing keyframes.
    for (int i=0;i<300;++i) {
        d.ProcessPacketAt(Packet(uint16_t(i+10),uint32_t(i),false,0,2,delta),80000+i);
        CHECK(d.GetPendingFrames()<=D::kMaxPendingFrames);
        CHECK(d.GetPendingBytes()<=D::kMaxPendingBytes);
    }
    d.Reset();received=0;
    // Loss => discard dependent frames; resume only on a real IDR with parameter sets.
    d.ProcessPacketAt(Packet(10,1,true,0,1,key),0);CHECK(received==1);
    d.ProcessPacketAt(Packet(12,3,false,0,1,delta),1);CHECK(received==1);
    d.OnTimerTickAt(11000);CHECK(received==1);
    d.ProcessPacketAt(Packet(13,4,true,0,1,key),12000);CHECK(received==2);
    // Flags alone must not make a P frame a random-access frame.
    d.Reset();received=0;d.ProcessPacketAt(Packet(0,0,true,0,1,delta),0);CHECK(received==0);
    // Current browser permits a full 1180-byte payload (1192-byte packet).
    std::vector<uint8_t> maxKey=key;maxKey.resize(1180,0xab);
    d.Reset();received=0;d.ProcessPacketAt(Packet(0,0,true,0,1,maxKey),0);CHECK(received==1);
    maxKey.push_back(0xab);d.ProcessPacketAt(Packet(1,1,true,0,1,maxKey),1);CHECK(received==1);
    // Explicit AVCC fallback; no guessing a partially valid length-prefixed packet.
    std::vector<uint8_t> avcc;CHECK(km::h264::AnnexBToLengthPrefixed4(key,avcc));
    d.Reset();received=0;d.ProcessPacketAt(Packet(0,0,true,0,1,avcc),0);CHECK(received==1);
    // Reentrant Reset from a callback must not deadlock.
    d.Reset();received=0;d.SetCallback([&](const uint8_t*,size_t,int64_t){++received;d.Reset();});
    d.ProcessPacketAt(Packet(0,0,true,0,1,key),0);CHECK(received==1 && d.GetPendingBytes()==0);
    d.SetCallback({});
    // Seeded malformed-packet corpus under ASan/UBSan. This is not a full fuzz campaign.
    std::mt19937 rng(0x4b4d);
    for (int i=0;i<20000;++i) {
        auto packet=Packet(uint16_t(rng()),rng(),bool(rng()&1),0,1,key);
        for (int j=0;j<4;++j) packet[rng()%packet.size()]=uint8_t(rng());
        if ((i%7)==0) packet.resize(rng()%packet.size());
        d.ProcessPacketAt(packet,100000+i);
        CHECK(d.GetPendingBytes()<=D::kMaxPendingBytes);
    }
    CHECK(!std::filesystem::exists("debug_stream_dump.h264"));
    CHECK(!std::filesystem::exists("debug_stream_dump.jsonl"));
    std::cout<<"datachannel_regression: all checks passed, malformed corpus=20000\n";
}
