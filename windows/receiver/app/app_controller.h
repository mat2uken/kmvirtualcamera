#pragma once

#include <windows.h>
#include <memory>
#include <thread>
#include <atomic>
#include "../ui/main_window.h"
#include "../signaling/win_http_client.h"
#include "../rtc/peer_connection_manager.h"
#include "../codec/h264_decoder.h"
#include "../media/nv12_converter.h"
#include "../media/pipe_publisher.h"
#include "../media/test_pattern_generator.h"
#include "../audio/audio_device_enumerator.h"
#include "../audio/wasapi_audio_renderer.h"
#include "../vcam/virtual_camera_registrar.h"

#include "lockfree_h264_queue.h"

namespace km::app {

class AppController {
public:
    AppController();
    ~AppController();

    bool Initialize(HINSTANCE hInstance, std::wstring baseUrl = L"http://127.0.0.1:8787");
    void RunMessageLoop();
    void Shutdown();

private:
    void StartNewSignalingSession();
    void SignalingWorkerProc();
    void VideoWorkerProc();
    void TestPatternWorkerProc();

    std::wstring baseUrl_{L"http://127.0.0.1:8787"};
    std::unique_ptr<ui::MainWindow> mainWindow_;
    std::unique_ptr<signaling::WinHttpClient> httpClient_;
    std::unique_ptr<rtc_net::PeerConnectionManager> rtcManager_;

    codec::H264Decoder h264Decoder_;
    media::Nv12Converter nv12Converter_;
    media::PipePublisher pipePublisher_;
    media::TestPatternGenerator testPatternGen_;
    audio::AudioDeviceEnumerator audioEnumerator_;
    audio::WasapiAudioRenderer audioRenderer_;
    vcam::VirtualCameraRegistrar vcamRegistrar_;

    std::vector<audio::AudioDevice> audioDevices_;
    int selectedAudioIndex_{0};

    std::atomic<bool> isSignalingRunning_{false};
    std::thread signalingThread_;

    std::atomic<bool> isVideoWorkerRunning_{false};
    std::thread videoWorkerThread_;
    LockFreeH264Queue lockFreeVideoQueue_;
    HANDLE hVideoFrameReadyEvent_{nullptr};

    std::atomic<bool> isTestPatternMode_{false};
    std::atomic<bool> isTestPatternWorkerRunning_{false};
    std::thread testPatternThread_;

    std::atomic<uint64_t> frameCount_{0};
    std::atomic<int> rotationDegrees_{0};
    std::atomic<ULONGLONG> lastDecodedFrameTick_{0};
};

} // namespace km::app
