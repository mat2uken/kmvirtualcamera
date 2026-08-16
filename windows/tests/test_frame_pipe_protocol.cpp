#include "../common/frame_pipe_protocol.h"
#include <cassert>
#include <iostream>
#include <vector>

void TestFramePipeProtocol() {
    std::cout << "[TEST] Running TestFramePipeProtocol..." << std::endl;

    // Test 1: Header size must be exactly 64 bytes
    static_assert(sizeof(km::protocol::FrameHeader) == 64);
    assert(km::protocol::kHeaderSize == 64);

    // Test 2: Serialization and Validation of default header
    auto header = km::protocol::CreateDefaultHeader(1, 1000000);
    std::vector<uint8_t> buffer(64);
    km::protocol::SerializeHeader(header, std::span<uint8_t, 64>(buffer.data(), 64));

    auto validated = km::protocol::DeserializeAndValidateHeader(buffer);
    assert(validated.has_value());
    assert(validated->sequence == 1);
    assert(validated->captureTimeUs == 1000000);
    assert(validated->width == 1280);
    assert(validated->height == 720);
    assert(validated->fourcc == km::protocol::kFourCcNv12);
    assert(validated->payloadBytes == 1382400);

    // Test 3: Rejection on bad magic
    buffer[0] = 'X';
    auto badMagic = km::protocol::DeserializeAndValidateHeader(buffer);
    assert(!badMagic.has_value());
    buffer[0] = 'W';

    // Test 4: Rejection on bad version
    buffer[8] = 2; // version = 2
    auto badVersion = km::protocol::DeserializeAndValidateHeader(buffer);
    assert(!badVersion.has_value());
    buffer[8] = 1;

    // Test 5: Rejection on reserved flags
    header.flags = 0x80; // bit 7 set
    km::protocol::SerializeHeader(header, std::span<uint8_t, 64>(buffer.data(), 64));
    auto badFlags = km::protocol::DeserializeAndValidateHeader(buffer);
    assert(!badFlags.has_value());

    // Test 6: Black NV12 buffer generation
    std::vector<uint8_t> nv12(km::protocol::kPayloadBytes);
    km::protocol::FillBlackNv12(nv12);
    // Y plane is 0x10
    assert(nv12[0] == 0x10);
    assert(nv12[1280 * 720 - 1] == 0x10);
    // UV plane is 0x80
    assert(nv12[1280 * 720] == 0x80);
    assert(nv12[km::protocol::kPayloadBytes - 1] == 0x80);

    std::cout << "[PASS] TestFramePipeProtocol passed completely." << std::endl;
}

int main() {
    TestFramePipeProtocol();
    return 0;
}
