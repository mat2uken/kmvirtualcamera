#pragma once

#include <windows.h>
#include <memory>
#include <thread>
#include <atomic>
#include "../ui/main_window.h"
#include "../signaling/win_http_client.h"
#include "../rtc/peer_connection_manager.h"
#include "../media/nv12_converter.h"
#include "../media/pipe_publisher.h"
#include "../audio/audio_device_enumerator.h"
#include "../audio/wasapi_audio_renderer.h"
#include "../vcam/virtual_camera_registrar.h"

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

    std::wstring baseUrl_{L"http://127.0.0.1:8787"};
    std::unique_ptr<ui::MainWindow> mainWindow_;
    std::unique_ptr<signaling::WinHttpClient> httpClient_;
    std::unique_ptr<rtc_net::PeerConnectionManager> rtcManager_;

    media::Nv12Converter nv12Converter_;
    media::PipePublisher pipePublisher_;
    audio::AudioDeviceEnumerator audioEnumerator_;
    audio::WasapiAudioRenderer audioRenderer_;
    vcam::VirtualCameraRegistrar vcamRegistrar_;

    std::vector<audio::AudioDevice> audioDevices_;
    int selectedAudioIndex_{0};

    std::atomic<bool> isSignalingRunning_{false};
    std::thread signalingThread_;

    std::vector<uint8_t> nv12Buffer_;
    std::mutex videoProcessMutex_;
};

} // namespace km::app
