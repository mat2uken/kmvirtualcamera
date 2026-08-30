#include "rtc/twcc_receiver.h"
#include <cassert>
#include <iostream>
#include <vector>

using namespace km::rtc_net;

void TestParseTransportSequence() {
    std::cout << "[Test] Running TestParseTransportSequence..." << std::endl;

    // Build a mock RTP packet with 0xBEDE header extension
    // Byte 0: V=2, P=0, X=1, CC=0 -> 0x90
    // Byte 1: M=0, PT=96 -> 0x60
    // Byte 2-3: Seq = 1000
    // Byte 4-7: Timestamp = 90000
    // Byte 8-11: SSRC = 0x12345678
    // Byte 12-13: Profile = 0xBEDE
    // Byte 14-15: Length in 32-bit words = 1 (4 bytes of extension data)
    // Byte 16: ID=3 (top 4 bits: 0x3), Len-1=1 (bottom 4 bits: 0x1) -> 0x31
    // Byte 17-18: Transport seq = 0x0539 (1337)
    // Byte 19: Padding (0x00)
    // Byte 20..27: Payload

    std::vector<uint8_t> rtpPacket = {
        0x90, 0x60, 0x03, 0xE8, // V=2, X=1, seq=1000
        0x00, 0x01, 0x5F, 0x90, // ts = 90000
        0x12, 0x34, 0x56, 0x78, // SSRC
        0xBE, 0xDE, 0x00, 0x01, // 0xBEDE, len=1 word (4 bytes)
        0x31, 0x05, 0x39, 0x00, // ID=3, len=2 (0x31), seq=1337 (0x0539), pad
        0xAA, 0xBB, 0xCC, 0xDD  // Payload
    };

    uint16_t outSeq = 0;
    bool found = TwccReceiver::ParseTransportSequence(rtpPacket.data(), rtpPacket.size(), 3, outSeq);
    assert(found);
    assert(outSeq == 1337);
    std::cout << "  - Correctly parsed transport sequence 1337 for ID=3" << std::endl;

    // Test with wrong ID
    uint16_t wrongSeq = 0;
    bool notFound = TwccReceiver::ParseTransportSequence(rtpPacket.data(), rtpPacket.size(), 5, wrongSeq);
    assert(!notFound);
    std::cout << "  - Correctly returned false for unmatching extension ID=5" << std::endl;

    // Test with packet without extension bit
    rtpPacket[0] = 0x80; // X=0
    bool noExt = TwccReceiver::ParseTransportSequence(rtpPacket.data(), rtpPacket.size(), 3, outSeq);
    assert(!noExt);
    std::cout << "  - Correctly returned false when X=0" << std::endl;
}

void TestTwccFeedbackGeneration() {
    std::cout << "[Test] Running TestTwccFeedbackGeneration..." << std::endl;

    TwccReceiver receiver;

    // Ingest 5 packets arriving 1ms apart (1000us)
    int64_t baseTimeUs = 1000000; // 1 second in microseconds
    for (uint16_t i = 0; i < 5; i++) {
        receiver.OnPacket(100 + i, baseTimeUs + i * 1000, 1200);
    }

    auto feedback = receiver.BuildFeedbackPacket(1, 0x12345678);
    assert(!feedback.empty());
    assert(feedback.size() >= 20); // Minimum RTCP TWCC header size

    // Check RTCP Header
    uint8_t byte0 = feedback[0];
    uint8_t version = (byte0 >> 6) & 0x03;
    uint8_t fmt = byte0 & 0x1F;
    uint8_t pt = feedback[1];

    assert(version == 2);
    assert(fmt == 15);
    assert(pt == 205); // RTPFB

    // Check base seq
    uint16_t baseSeq = (static_cast<uint16_t>(feedback[12]) << 8) | feedback[13];
    uint16_t statusCount = (static_cast<uint16_t>(feedback[14]) << 8) | feedback[15];
    assert(baseSeq == 100);
    assert(statusCount == 5);

    std::cout << "  - Generated valid TWCC RTCP packet: size=" << feedback.size()
              << " bytes, baseSeq=" << baseSeq << ", count=" << statusCount << std::endl;

    // Building again immediately should return empty (no new pending packets)
    auto emptyFb = receiver.BuildFeedbackPacket(1, 0x12345678);
    assert(emptyFb.empty());
    std::cout << "  - Subsequent build returned empty as expected" << std::endl;
}

void TestTwccWrapAround() {
    std::cout << "[Test] Running TestTwccWrapAround..." << std::endl;

    TwccReceiver receiver;

    // Packet near 65535 and wrapping to 0, 1
    receiver.OnPacket(65534, 1000000, 1000);
    receiver.OnPacket(65535, 1001000, 1000);
    receiver.OnPacket(0,     1002000, 1000);
    receiver.OnPacket(1,     1003000, 1000);

    auto feedback = receiver.BuildFeedbackPacket(1, 0x12345678);
    assert(!feedback.empty());

    uint16_t baseSeq = (static_cast<uint16_t>(feedback[12]) << 8) | feedback[13];
    uint16_t statusCount = (static_cast<uint16_t>(feedback[14]) << 8) | feedback[15];
    assert(baseSeq == 65534);
    assert(statusCount == 4);

    std::cout << "  - Correctly handled 16-bit sequence number wrap-around (65534 -> 1)" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " TWCC Receiver Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    try {
        TestParseTransportSequence();
        TestTwccFeedbackGeneration();
        TestTwccWrapAround();
        std::cout << "\n>>> ALL TWCC TESTS PASSED! <<<\n" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
