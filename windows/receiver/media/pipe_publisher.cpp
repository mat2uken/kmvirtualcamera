#include "pipe_publisher.h"
#include <sddl.h>

namespace km::media {

PipePublisher::PipePublisher() {
    latestPayload_.resize(protocol::kPayloadBytes);
    protocol::FillBlackNv12(latestPayload_);
    hStopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    hNewFrameEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    InitializeSRWLock(&srwLock_);
}

PipePublisher::~PipePublisher() {
    Stop();
    if (hStopEvent_) {
        CloseHandle(hStopEvent_);
        hStopEvent_ = nullptr;
    }
    if (hNewFrameEvent_) {
        CloseHandle(hNewFrameEvent_);
        hNewFrameEvent_ = nullptr;
    }
}

void PipePublisher::Start() {
    if (isRunning_.exchange(true)) return;
    ResetEvent(hStopEvent_);
    serverThread_ = std::thread(&PipePublisher::ServerThreadProc, this);
}

void PipePublisher::Stop() {
    if (!isRunning_.exchange(false)) return;
    SetEvent(hStopEvent_);
    SetEvent(hNewFrameEvent_);
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
}

void PipePublisher::PublishFrame(const uint8_t* nv12Data, size_t dataSize, int64_t captureTimeUs) {
    if (!nv12Data || dataSize != protocol::kPayloadBytes || !isRunning_) return;

    AcquireSRWLockExclusive(&srwLock_);
    std::memcpy(latestPayload_.data(), nv12Data, protocol::kPayloadBytes);
    sequence_++;
    latestCaptureTimeUs_ = captureTimeUs;
    ReleaseSRWLockExclusive(&srwLock_);
    SetEvent(hNewFrameEvent_);
}

bool PipePublisher::WriteExact(HANDLE hPipe, const uint8_t* buffer, DWORD bytesToWrite, OVERLAPPED& ov, HANDLE hStopEvent) {
    DWORD totalWritten = 0;
    while (totalWritten < bytesToWrite && isRunning_) {
        DWORD bytesWritten = 0;
        ResetEvent(ov.hEvent);
        BOOL ok = WriteFile(hPipe, buffer + totalWritten, bytesToWrite - totalWritten, nullptr, &ov);
        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                HANDLE events[2] = { ov.hEvent, hStopEvent };
                DWORD waitRes = WaitForMultipleObjects(2, events, FALSE, INFINITE);
                if (waitRes == WAIT_OBJECT_0 + 1 || !isRunning_) {
                    CancelIo(hPipe);
                    return false;
                }
                if (!GetOverlappedResult(hPipe, &ov, &bytesWritten, TRUE)) {
                    return false;
                }
            } else {
                return false;
            }
        }
        totalWritten += bytesWritten;
    }
    return totalWritten == bytesToWrite;
}

void PipePublisher::ServerThreadProc() {
    OVERLAPPED connectOv{};
    connectOv.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    OVERLAPPED writeOv{};
    writeOv.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    std::vector<uint8_t> framePayload(protocol::kPayloadBytes);
    std::vector<uint8_t> headerBytes(protocol::kHeaderSize);

    while (isRunning_) {
        // Create Pipe instance
        HANDLE hPipe = CreateNamedPipeW(
            L"\\\\.\\pipe\\WebRtcBridge.VirtualCamera.v1",
            PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, // 1 instance
            static_cast<DWORD>(protocol::kPayloadBytes * 2),
            0,
            0,
            nullptr
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            DWORD waitRes = WaitForSingleObject(hStopEvent_, 500);
            if (waitRes == WAIT_OBJECT_0 || !isRunning_) break;
            continue;
        }

        // ConnectNamedPipe with overlapped I/O
        ResetEvent(connectOv.hEvent);
        BOOL connected = ConnectNamedPipe(hPipe, &connectOv);
        if (!connected) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                HANDLE events[2] = { connectOv.hEvent, hStopEvent_ };
                DWORD waitRes = WaitForMultipleObjects(2, events, FALSE, INFINITE);
                if (waitRes == WAIT_OBJECT_0 + 1 || !isRunning_) {
                    CancelIo(hPipe);
                    CloseHandle(hPipe);
                    break;
                }
            } else if (err != ERROR_PIPE_CONNECTED) {
                CloseHandle(hPipe);
                continue;
            }
        }

        // Connected to Virtual Camera Media Source client
        while (isRunning_) {
            uint64_t seq = 0;
            int64_t tsUs = 0;

            // Wait for new frame using lightweight event instead of condition_variable
            HANDLE waitEvents[2] = { hNewFrameEvent_, hStopEvent_ };
            DWORD waitRes = WaitForMultipleObjects(2, waitEvents, FALSE, INFINITE);
            if (waitRes == WAIT_OBJECT_0 + 1 || !isRunning_) break;

            // Copy latest frame under SRWLOCK (shared read would work but exclusive is fine for single consumer)
            AcquireSRWLockShared(&srwLock_);
            std::memcpy(framePayload.data(), latestPayload_.data(), protocol::kPayloadBytes);
            seq = sequence_;
            tsUs = latestCaptureTimeUs_;
            ReleaseSRWLockShared(&srwLock_);

            // Prepare header
            protocol::FrameHeader header = protocol::CreateDefaultHeader(seq, tsUs);
            std::span<uint8_t, protocol::kHeaderSize> headerSpan(headerBytes.data(), protocol::kHeaderSize);
            protocol::SerializeHeader(header, headerSpan);

            // Write header
            if (!WriteExact(hPipe, headerBytes.data(), protocol::kHeaderSize, writeOv, hStopEvent_)) {
                break; // Client disconnected
            }

            // Write payload
            if (!WriteExact(hPipe, framePayload.data(), protocol::kPayloadBytes, writeOv, hStopEvent_)) {
                break; // Client disconnected
            }
        }

        FlushFileBuffers(hPipe);
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }

    if (connectOv.hEvent) CloseHandle(connectOv.hEvent);
    if (writeOv.hEvent) CloseHandle(writeOv.hEvent);
}

} // namespace km::media
