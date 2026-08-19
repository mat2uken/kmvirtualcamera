#pragma once

#include "../../common/frame_pipe_protocol.h"
#include "../../common/shared_memory_frame.h"
#include "../../common/dxgi_shared_texture.h"
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
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

    // Publishes an NV12 frame to pipe server, CPU shared memory, and DXGI GPU shared texture.
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

    shm::SharedMemoryPublisher shmPublisher_;
    dxgi::DxgiTexturePublisher dxgiPublisher_;
    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
};

} // namespace km::media
