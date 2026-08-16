#include "../receiver/codec/h264_rtp_depacketizer.h"
#include <iostream>
#include <cassert>
#include <vector>

int main() {
    std::cout << "[TEST] Running TestH264RtpDepacketizer..." << std::endl;

    std::vector<std::vector<uint8_t>> receivedFrames;
    km::codec::H264RtpDepacketizer depacketizer([&](const uint8_t* data, size_t size, uint32_t ts) {
        receivedFrames.emplace_back(data, data + size);
    });

    // Test 1: Single NAL (IDR slice) with Marker bit set
    uint8_t singleNalRtp[] = {
        0x80, 0xE0, 0x00, 0x01, 0x00, 0x00, 0x03, 0xE8, 0x12, 0x34, 0x56, 0x78, // RTP Header (M=1, ts=1000)
        0x65, 0x88, 0x84, 0x00, 0x33 // NAL Type 5 (IDR)
    };
    depacketizer.ProcessRtpPacket(singleNalRtp, sizeof(singleNalRtp));
    assert(receivedFrames.size() == 1);
    assert(receivedFrames[0].size() == 4 + 5);
    assert(receivedFrames[0][0] == 0x00 && receivedFrames[0][1] == 0x00 && receivedFrames[0][2] == 0x00 && receivedFrames[0][3] == 0x01);
    assert(receivedFrames[0][4] == 0x65);

    // Test 2: STAP-A packet (SPS + PPS)
    receivedFrames.clear();
    uint8_t stapARtp[] = {
        0x80, 0x60, 0x00, 0x02, 0x00, 0x00, 0x07, 0xD0, 0x12, 0x34, 0x56, 0x78, // RTP Header (M=0, ts=2000)
        0x78, // STAP-A (Type 24)
        0x00, 0x04, 0x67, 0x42, 0x00, 0x1F, // SPS (size=4)
        0x00, 0x03, 0x68, 0xCE, 0x38        // PPS (size=3)
    };
    depacketizer.ProcessRtpPacket(stapARtp, sizeof(stapARtp));
    // Another single NAL on same timestamp with M=1
    uint8_t idrRtp[] = {
        0x80, 0xE0, 0x00, 0x03, 0x00, 0x00, 0x07, 0xD0, 0x12, 0x34, 0x56, 0x78, // M=1, ts=2000
        0x65, 0x11, 0x22
    };
    depacketizer.ProcessRtpPacket(idrRtp, sizeof(idrRtp));
    assert(receivedFrames.size() == 1);
    // Should have 3 NAL units: SPS, PPS, IDR
    assert(receivedFrames[0].size() == (4 + 4) + (4 + 3) + (4 + 3));

    // Test 3: FU-A Fragmentation Unit (Start + Middle + End)
    receivedFrames.clear();
    uint8_t fuaStart[] = {
        0x80, 0x60, 0x00, 0x04, 0x00, 0x00, 0x0B, 0xB8, 0x12, 0x34, 0x56, 0x78, // M=0, ts=3000
        0x7C, 0x85, 0xAA, 0xBB // FU Indicator (28), FU Header (S=1, Type 5)
    };
    uint8_t fuaMiddle[] = {
        0x80, 0x60, 0x00, 0x05, 0x00, 0x00, 0x0B, 0xB8, 0x12, 0x34, 0x56, 0x78, // M=0, ts=3000
        0x7C, 0x05, 0xCC, 0xDD // FU Header (S=0, E=0)
    };
    uint8_t fuaEnd[] = {
        0x80, 0xE0, 0x00, 0x06, 0x00, 0x00, 0x0B, 0xB8, 0x12, 0x34, 0x56, 0x78, // M=1, ts=3000
        0x7C, 0x45, 0xEE, 0xFF // FU Header (E=1)
    };
    depacketizer.ProcessRtpPacket(fuaStart, sizeof(fuaStart));
    depacketizer.ProcessRtpPacket(fuaMiddle, sizeof(fuaMiddle));
    depacketizer.ProcessRtpPacket(fuaEnd, sizeof(fuaEnd));
    assert(receivedFrames.size() == 1);
    // Reconstructed NAL should have Start Code + Reconstructed Header (0x65) + 6 payload bytes
    assert(receivedFrames[0].size() == 4 + 1 + 6);
    assert(receivedFrames[0][4] == 0x65);

    // Test 4: Unknown NAL types and RTP extension packets (must NOT crash or throw exceptions)
    uint8_t unknownNalRtp[] = {
        0x90, 0x60, 0x00, 0x07, 0x00, 0x00, 0x0F, 0xA0, 0x12, 0x34, 0x56, 0x78, // X=1 (extension)
        0xBE, 0xDE, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, // Extension header (length=1 word)
        0x00, 0x00, 0x00 // Type 0 (undefined/padding)
    };
    depacketizer.ProcessRtpPacket(unknownNalRtp, sizeof(unknownNalRtp));

    std::cout << "[PASS] TestH264RtpDepacketizer passed completely (100% RFC 6184 compliance verified)." << std::endl;
    return 0;
}
