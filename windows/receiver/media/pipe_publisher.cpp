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
    shmPublisher_.Open();

    if (!d3dDevice_) {
        D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL featureLevel;
        HRESULT hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            featureLevels,
            2,
            D3D11_SDK_VERSION,
            &d3dDevice_,
            &featureLevel,
            &d3dContext_
        );
        if (SUCCEEDED(hr) && d3dDevice_) {
            dxgiPublisher_.Initialize(d3dDevice_.Get(), protocol::kWidth, protocol::kHeight);
        }
    }

    ResetEvent(hStopEvent_);
    serverThread_ = std::thread(&PipePublisher::ServerThreadProc, this);
}

void PipePublisher::Stop() {
    if (!isRunning_.exchange(false)) return;
    dxgiPublisher_.Close();
    d3dContext_.Reset();
    d3dDevice_.Reset();
    shmPublisher_.Close();
    SetEvent(hStopEvent_);
    SetEvent(hNewFrameEvent_);
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
}

void PipePublisher::PublishFrame(const uint8_t* nv12Data, size_t dataSize, int64_t captureTimeUs) {
    if (!nv12Data || dataSize != protocol::kPayloadBytes || !isRunning_) return;

    if (dxgiPublisher_.IsInitialized() && d3dContext_) {
        dxgiPublisher_.PublishNv12Frame(d3dContext_.Get(), nv12Data, protocol::kWidth, protocol::kHeight, captureTimeUs);
    }

    shmPublisher_.PublishFrame(nv12Data, dataSize, captureTimeUs);

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
        } else {
            if (!GetOverlappedResult(hPipe, &ov, &bytesWritten, FALSE)) {
                return false;
            }
        }
        if (bytesWritten == 0) return false;
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

    // Prepare Security Descriptor allowing NT AUTHORITY\LOCAL SERVICE, ALL APPLICATION PACKAGES, and Everyone
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;
    PSECURITY_DESCRIPTOR pSd = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;WD)(A;;GA;;;AC)(A;;GA;;;LS)(A;;GA;;;SY)(A;;GA;;;AU)S:(ML;;NW;;;LW)",
            SDDL_REVISION_1,
            &pSd,
            nullptr)) {
        sa.lpSecurityDescriptor = pSd;
    }

    uint64_t standbySeq = 0;

    while (isRunning_) {
        // Create Pipe instance with permissive DACL and multiple instances support
        HANDLE hPipe = CreateNamedPipeW(
            L"\\\\.\\pipe\\WebRtcBridge.VirtualCamera.v1",
            PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            static_cast<DWORD>(protocol::kPayloadBytes * 2),
            0,
            0,
            &sa
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
                DWORD dwDummy = 0;
                GetOverlappedResult(hPipe, &connectOv, &dwDummy, TRUE);
            } else if (err != ERROR_PIPE_CONNECTED) {
                CloseHandle(hPipe);
                continue;
            }
        }

        // Connected to Virtual Camera Media Source client
        while (isRunning_) {
            uint64_t seq = 0;
            int64_t tsUs = 0;

            // Wait for new frame with 33ms timeout (~30fps heartbeat) to ensure standby frames stream smoothly
            HANDLE waitEvents[2] = { hNewFrameEvent_, hStopEvent_ };
            DWORD waitRes = WaitForMultipleObjects(2, waitEvents, FALSE, 33);
            if (waitRes == WAIT_OBJECT_0 + 1 || !isRunning_) break;

            // Copy latest frame under SRWLOCK
            AcquireSRWLockShared(&srwLock_);
            std::memcpy(framePayload.data(), latestPayload_.data(), protocol::kPayloadBytes);
            seq = sequence_;
            tsUs = latestCaptureTimeUs_;
            ReleaseSRWLockShared(&srwLock_);

            if (seq == 0) {
                seq = ++standbySeq;
                tsUs = static_cast<int64_t>(GetTickCount64() * 1000);
            }

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

    if (pSd) {
        LocalFree(pSd);
    }
    if (connectOv.hEvent) CloseHandle(connectOv.hEvent);
    if (writeOv.hEvent) CloseHandle(writeOv.hEvent);
}

} // namespace km::media
