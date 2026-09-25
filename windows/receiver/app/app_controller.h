#pragma once
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
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
#include "../../../shared/km/bounded_video_queue.h"
namespace km::app {
class AppController {
public:
    AppController();
    ~AppController();
    bool Initialize(HINSTANCE instance, std::wstring baseUrl = L"http://127.0.0.1:8787");
    void RunMessageLoop();
    void Shutdown();
private:
    struct UiWork { uint64_t generation; std::function<void()> run; };
    static constexpr UINT kUiWorkMessage = WM_APP + 0x4b;
    void PostUi(uint64_t generation, std::function<void()> work);
    static LRESULT CALLBACK UiDispatchProc(HWND, UINT, WPARAM, LPARAM);
    void StartNewSignalingSession();
    void SignalingWorkerProc(uint64_t generation);
    void VideoWorkerProc();
    void TestPatternWorkerProc(); // sole 30fps camera publisher, for live AND idle frames
    std::unique_ptr<ui::MainWindow> mainWindow_;
    std::unique_ptr<signaling::WinHttpClient> httpClient_;
    std::unique_ptr<rtc_net::PeerConnectionManager> rtcManager_;
    media::PipePublisher pipePublisher_;
    audio::AudioDeviceEnumerator audioEnumerator_;
    audio::WasapiAudioRenderer audioRenderer_;
    vcam::VirtualCameraRegistrar vcamRegistrar_;
    std::vector<audio::AudioDevice> audioDevices_;
    km::BoundedVideoQueue videoQueue_;
    std::atomic<bool> isSignalingRunning_{false}, isVideoWorkerRunning_{false}, isOutputRunning_{false};
    std::atomic<bool> testPatternMode_{false}, acceptingUi_{false}, shuttingDown_{false};
    std::atomic<int> rotationDegrees_{0};
    std::atomic<unsigned> queuedUi_{0};
    // Shutdown diagnostics: which call each worker is sitting in while a join stalls.
    std::atomic<int> videoWorkerStep_{0}, outputWorkerStep_{0};
    std::atomic<bool> videoWorkerJoined_{false}, outputWorkerJoined_{false};
    DWORD uiThread_ = 0;
    HWND uiDispatchWindow_ = nullptr;
    HANDLE videoReady_ = nullptr;
    std::thread signalingThread_, videoThread_, outputThread_;
    std::mutex frameMutex_, outputWaitMutex_;
    std::condition_variable outputWake_;
    std::shared_ptr<const std::vector<uint8_t>> latestNv12_;
    km::QueuedAccessUnit latestIdentity_;
    uint64_t latestArrivalNs_ = 0;
};
} // namespace km::app
