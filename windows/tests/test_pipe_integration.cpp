#include "../common/frame_pipe_protocol.h"
#include "../receiver/media/pipe_publisher.h"
#include "../virtual-camera/media-source/pipe_frame_receiver.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

void TestPipeIntegration() {
    std::cout << "[TEST] Running TestPipeIntegration (Live Named Pipe IPC)..." << std::endl;

    km::media::PipePublisher publisher;
    publisher.Start();

    // Small sleep to ensure pipe server is listening
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    km::vcam::PipeFrameReceiver receiver;
    receiver.Start();

    // Create custom test frame with recognizable pattern
    std::vector<uint8_t> testNv12(km::protocol::kPayloadBytes);
    km::protocol::FillBlackNv12(testNv12);
    // Mark specific signature in payload
    testNv12[100] = 0xAA;
    testNv12[101] = 0xBB;
    testNv12[1000] = 0xCC;

    // Publish frames continuously
    for (int i = 1; i <= 10; ++i) {
        publisher.PublishFrame(testNv12.data(), km::protocol::kPayloadBytes, i * 33333);
        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30fps
    }

    // Wait for receiver to read latest frame
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<uint8_t> receivedFrame(km::protocol::kPayloadBytes);
    uint64_t seq = 0;
    int64_t tsUs = 0;
    bool ok = receiver.GetLatestFrame(receivedFrame, seq, tsUs);

    assert(ok);
    assert(seq >= 1);
    assert(tsUs > 0);
    assert(receivedFrame[100] == 0xAA);
    assert(receivedFrame[101] == 0xBB);
    assert(receivedFrame[1000] == 0xCC);

    std::cout << "  [OK] Successfully received frame sequence=" << seq << " ts=" << tsUs << " us" << std::endl;

    receiver.Stop();
    publisher.Stop();

    std::cout << "[PASS] TestPipeIntegration passed completely." << std::endl;
}

int main() {
    TestPipeIntegration();
    return 0;
}
