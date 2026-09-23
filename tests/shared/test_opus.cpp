#include "receiver/audio/opus_rtp_decoder.h"
#include <iostream>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::cerr<<__LINE__<<": " #x "\n"; return 1; } } while(false)
static std::vector<uint8_t> packet(uint16_t sequence,uint32_t timestamp,uint32_t ssrc=1) {
    return {0x80,111,uint8_t(sequence>>8),uint8_t(sequence),uint8_t(timestamp>>24),uint8_t(timestamp>>16),uint8_t(timestamp>>8),uint8_t(timestamp),
        uint8_t(ssrc>>24),uint8_t(ssrc>>16),uint8_t(ssrc>>8),uint8_t(ssrc),0xf8,0xff,0xfe};
}
int main() {
    km::audio::OpusRtpDecoder decoder;size_t calls=0,elements=0;bool valid=true;
    CHECK(decoder.Configure(111,[&](const int16_t* pcm,size_t n,int channels,int rate){
        valid=valid&&pcm&&channels==2&&rate==48000&&n%2==0&&n<=11520;++calls;elements+=n;
    }));
    decoder.Receive(packet(65535,0),0);CHECK(calls==0);decoder.Tick(20000);CHECK(calls==1&&elements==1920);
    decoder.Receive(packet(0,960),20001);decoder.Tick(40001);CHECK(calls==2&&elements==3840);
    decoder.Receive(packet(0,960),40002);decoder.Tick(70000);CHECK(calls==2);
    // One missing 20ms packet -> 960 frames/ch PLC, then the received packet.
    decoder.Receive(packet(2,2880),70001);decoder.Tick(110001);CHECK(calls==4&&elements==7680);
    decoder.Reset();decoder.Receive(packet(1,960),0);decoder.Receive(packet(0,0),1);decoder.Tick(21000);CHECK(calls==6);
    decoder.Receive(packet(0,0,2),30000);decoder.Tick(50000);CHECK(calls==7);
    CHECK(valid);std::cout<<"opus_rtp_decode: actual libopus decode, interleaved size, PLC, reorder, wrap and SSRC reset passed\n";
}
