#pragma once

#include "../../common/frame_pipe_protocol.h"
#include <windows.h>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>

namespace km::vcam {

class PipeFrameReceiver {
public:
    PipeFrameReceiver();
    ~PipeFrameReceiver();

    void Start();
    void Stop();

    // Copies the latest frame payload. Returns true if frame is valid, false if stale/black.
    bool GetLatestFrame(std::vector<uint8_t>& outBuffer, uint64_t& outSequence, int64_t& outTimestampUs);

private:
    void ReaderThreadProc();
    bool ReadExact(HANDLE hPipe, uint8_t* buffer, DWORD bytesToRead, OVERLAPPED& ov, HANDLE hStopEvent);

    std::atomic<bool> isRunning_{false};
    std::thread readerThread_;
    HANDLE hStopEvent_{nullptr};

    std::mutex frameMutex_;
    std::vector<uint8_t> activeBuffer_;
    uint64_t latestSequence_{0};
    int64_t latestTimestampUs_{0};
    ULONGLONG lastFrameArrivalTick_{0};
    bool hasValidFrame_{false};
};

} // namespace km::vcam
