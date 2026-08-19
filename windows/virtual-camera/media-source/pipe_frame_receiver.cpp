#include "pipe_frame_receiver.h"
#include "vcam_logger.h"
#include <chrono>

namespace km::vcam {

PipeFrameReceiver::PipeFrameReceiver() {
    activeBuffer_.resize(protocol::kPayloadBytes);
    protocol::FillBlackNv12(activeBuffer_);
    hStopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

PipeFrameReceiver::~PipeFrameReceiver() {
    Stop();
    if (hStopEvent_) {
        CloseHandle(hStopEvent_);
        hStopEvent_ = nullptr;
    }
}

void PipeFrameReceiver::Start() {
    if (isRunning_.exchange(true)) return;
    ResetEvent(hStopEvent_);
    readerThread_ = std::thread(&PipeFrameReceiver::ReaderThreadProc, this);
}

void PipeFrameReceiver::Stop() {
    if (!isRunning_.exchange(false)) return;
    SetEvent(hStopEvent_);
    if (readerThread_.joinable()) {
        readerThread_.join();
    }
}

bool PipeFrameReceiver::GetLatestFrame(std::vector<uint8_t>& outBuffer, uint64_t& outSequence, int64_t& outTimestampUs) {
    std::lock_guard<std::mutex> lock(frameMutex_);
    if (outBuffer.size() != protocol::kPayloadBytes) {
        outBuffer.resize(protocol::kPayloadBytes);
    }

    ULONGLONG now = GetTickCount64();
    // Stale check: if no frame received for > 2000ms, fallback to black
    if (!hasValidFrame_ || (now - lastFrameArrivalTick_ > 2000)) {
        protocol::FillBlackNv12(outBuffer);
        outSequence = latestSequence_;
        outTimestampUs = latestTimestampUs_;
        return false;
    }

    std::memcpy(outBuffer.data(), activeBuffer_.data(), protocol::kPayloadBytes);
    outSequence = latestSequence_;
    outTimestampUs = latestTimestampUs_;
    return true;
}

bool PipeFrameReceiver::ReadExact(HANDLE hPipe, uint8_t* buffer, DWORD bytesToRead, OVERLAPPED& ov, HANDLE hStopEvent) {
    DWORD totalRead = 0;
    while (totalRead < bytesToRead && isRunning_) {
        DWORD bytesRead = 0;
        ResetEvent(ov.hEvent);
        BOOL ok = ReadFile(hPipe, buffer + totalRead, bytesToRead - totalRead, nullptr, &ov);
        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                HANDLE events[2] = { ov.hEvent, hStopEvent };
                DWORD waitRes = WaitForMultipleObjects(2, events, FALSE, INFINITE);
                if (waitRes == WAIT_OBJECT_0 + 1 || !isRunning_) {
                    CancelIo(hPipe);
                    return false;
                }
                if (!GetOverlappedResult(hPipe, &ov, &bytesRead, TRUE)) {
                    return false;
                }
            } else {
                return false;
            }
        } else {
            if (!GetOverlappedResult(hPipe, &ov, &bytesRead, FALSE)) {
                return false;
            }
        }
        if (bytesRead == 0) return false;
        totalRead += bytesRead;
    }
    return totalRead == bytesToRead;
}

void PipeFrameReceiver::ReaderThreadProc() {
    std::vector<uint8_t> headerBuffer(protocol::kHeaderSize);
    std::vector<uint8_t> scratchPayload(protocol::kPayloadBytes);

    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    while (isRunning_) {
        // Attempt to connect to pipe
        LogVcam(L"[PipeFrameReceiver] Attempting to connect to named pipe...");
        HANDLE hPipe = CreateFileW(
            L"\\\\.\\pipe\\WebRtcBridge.VirtualCamera.v1",
            GENERIC_READ,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            nullptr
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            LogVcam(L"[PipeFrameReceiver] CreateFileW failed err=%u (0x%08X)", err, err);
            // Wait with backoff or stop event
            DWORD waitRes = WaitForSingleObject(hStopEvent_, 200);
            if (waitRes == WAIT_OBJECT_0 || !isRunning_) break;
            continue;
        }

        LogVcam(L"[PipeFrameReceiver] Successfully connected to pipe publisher!");

        // Successfully connected to pipe server
        while (isRunning_) {
            // 1. Read header (64 bytes)
            if (!ReadExact(hPipe, headerBuffer.data(), protocol::kHeaderSize, ov, hStopEvent_)) {
                LogVcam(L"[PipeFrameReceiver] Read header failed, disconnecting");
                break; // Pipe disconnected or stopped
            }

            // 2. Validate header
            auto headerOpt = protocol::DeserializeAndValidateHeader(headerBuffer);
            if (!headerOpt.has_value()) {
                break; // Protocol violation -> drop connection and reconnect
            }

            // 3. Read payload (1,382,400 bytes)
            if (!ReadExact(hPipe, scratchPayload.data(), protocol::kPayloadBytes, ov, hStopEvent_)) {
                break;
            }

            // 4. Atomically swap into active buffer
            {
                std::lock_guard<std::mutex> lock(frameMutex_);
                std::memcpy(activeBuffer_.data(), scratchPayload.data(), protocol::kPayloadBytes);
                latestSequence_ = headerOpt->sequence;
                latestTimestampUs_ = headerOpt->captureTimeUs;
                lastFrameArrivalTick_ = GetTickCount64();
                hasValidFrame_ = true;
            }
        }

        CloseHandle(hPipe);
        {
            std::lock_guard<std::mutex> lock(frameMutex_);
            hasValidFrame_ = false;
        }
    }

    if (ov.hEvent) {
        CloseHandle(ov.hEvent);
    }
}

} // namespace km::vcam
