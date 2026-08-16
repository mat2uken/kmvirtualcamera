#pragma once

#include "../../common/frame_pipe_protocol.h"
#include <windows.h>
#include <vector>
#include <thread>
#include <atomic>

namespace km::media {

class PipePublisher {
public:
    PipePublisher();
    ~PipePublisher();

    void Start();
    void Stop();

    // Publishes a 720p30 NV12 frame to the pipe server. Non-blocking; drops intermediate frames.
    void PublishFrame(const uint8_t* nv12Data, size_t dataSize, int64_t captureTimeUs = 0);

private:
    void ServerThreadProc();
    bool WriteExact(HANDLE hPipe, const uint8_t* buffer, DWORD bytesToWrite, OVERLAPPED& ov, HANDLE hStopEvent);

    std::atomic<bool> isRunning_{false};
    std::thread serverThread_;
    HANDLE hStopEvent_{nullptr};
    HANDLE hNewFrameEvent_{nullptr};

    SRWLOCK srwLock_ = SRWLOCK_INIT;
    std::vector<uint8_t> latestPayload_;
    uint64_t sequence_{0};
    int64_t latestCaptureTimeUs_{0};
};

} // namespace km::media
