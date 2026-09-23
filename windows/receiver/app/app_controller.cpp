#include "app_controller.h"
#include "../../../shared/km/timing.h"
#include "../../../shared/km/json.h"
#include <avrt.h>
#include <algorithm>
#include <chrono>
#include <iostream>
namespace km::app {
namespace {
uint64_t monoNs() { return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
}
AppController::AppController() { videoReady_ = CreateEventW(nullptr, FALSE, FALSE, nullptr); }
AppController::~AppController() { Shutdown(); if (videoReady_) CloseHandle(videoReady_); }
void AppController::PostUi(uint64_t generation, std::function<void()> fn) {
    if (!acceptingUi_) return;
    if (queuedUi_.fetch_add(1) >= 128) { --queuedUi_; return; }
    auto work = std::make_unique<UiWork>(UiWork{generation, std::move(fn)});
    if (PostMessageW(uiDispatchWindow_, kUiWorkMessage, 0, reinterpret_cast<LPARAM>(work.get()))) work.release();
    else --queuedUi_;
}
LRESULT CALLBACK AppController::UiDispatchProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    auto* owner = reinterpret_cast<AppController*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == kUiWorkMessage && owner) {
        std::unique_ptr<UiWork> work(reinterpret_cast<UiWork*>(lparam)); --owner->queuedUi_;
        if (owner->acceptingUi_ && (work->generation == 0 || work->generation == owner->videoQueue_.generation())) {
            try { work->run(); } catch (const std::exception&) { std::cerr << "[UI] Posted update failed\n"; }
        }
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
bool AppController::Initialize(HINSTANCE instance, std::wstring baseUrl) {
    if (!videoReady_ || shuttingDown_) return false;
    uiThread_ = GetCurrentThreadId(); MSG ignored{}; PeekMessageW(&ignored, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    httpClient_ = std::make_unique<signaling::WinHttpClient>(std::move(baseUrl));
    rtcManager_ = std::make_unique<rtc_net::PeerConnectionManager>();
    mainWindow_ = std::make_unique<ui::MainWindow>(); if (!mainWindow_->Create(instance)) return false;
    WNDCLASSW dispatchClass{};
    dispatchClass.hInstance = instance; dispatchClass.lpfnWndProc = &AppController::UiDispatchProc;
    dispatchClass.lpszClassName = L"KMVirtualCamera.UiDispatch";
    if (!RegisterClassW(&dispatchClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    uiDispatchWindow_ = CreateWindowExW(0, dispatchClass.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
    if (!uiDispatchWindow_) return false;
    acceptingUi_ = true;
    audioDevices_ = audioEnumerator_.EnumerateRenderDevices();
    int selected = 0;
    for (size_t i = 0; i < audioDevices_.size(); ++i) if (audioDevices_[i].isCableInput) { selected = int(i); break; }
    if (!audioDevices_.empty() && audioRenderer_.Initialize(audioDevices_[size_t(selected)].id)) audioRenderer_.Start();
    mainWindow_->SetAudioDevices(audioDevices_, selected);
    mainWindow_->SetOnAudioDeviceChanged([this](int index) {
        if (index >= 0 && size_t(index) < audioDevices_.size() && audioRenderer_.Initialize(audioDevices_[size_t(index)].id)) audioRenderer_.Start();
    });
    pipePublisher_.Start();
    const bool registered = vcam::VirtualCameraRegistrar::IsVirtualCameraRegistered();
    mainWindow_->SetVirtualCameraRegistered(registered);
    mainWindow_->SetVirtualCameraStatus(vcamRegistrar_.StartVirtualCamera(L"WebRTC Bridge Virtual Camera"));
    mainWindow_->SetOnToggleVirtualCamera([this] {
        if (vcamRegistrar_.IsRunning()) { vcamRegistrar_.StopVirtualCamera(); mainWindow_->SetVirtualCameraStatus(false); }
        else mainWindow_->SetVirtualCameraStatus(vcamRegistrar_.StartVirtualCamera(L"WebRTC Bridge Virtual Camera"));
    });
    mainWindow_->SetOnRegisterVirtualCamera([this] {
        const bool ok = vcam::VirtualCameraRegistrar::RegisterVirtualCameraWithElevation(mainWindow_->GetHwnd());
        const bool registeredNow = vcam::VirtualCameraRegistrar::IsVirtualCameraRegistered();
        mainWindow_->SetVirtualCameraRegistered(registeredNow);
        if (registeredNow) mainWindow_->SetVirtualCameraStatus(vcamRegistrar_.StartVirtualCamera());
        MessageBoxW(mainWindow_->GetHwnd(), registeredNow ? L"仮想カメラを登録しました。" : ok ? L"登録状態を確認してください。" : L"登録が中止されたか失敗しました。", L"仮想カメラ登録", MB_OK);
    });
    mainWindow_->SetOnCheckCameras([this] {
        auto cameras = vcam::VirtualCameraRegistrar::EnumerateSystemCameras();
        std::wstring text = L"カメラ一覧\n";
        for (const auto& camera : cameras) text += camera.friendlyName + (camera.isVirtualCamera ? L" [KM仮想カメラ]\n" : L"\n");
        mainWindow_->SetVirtualCameraRegistered(vcam::VirtualCameraRegistrar::IsVirtualCameraRegistered());
        MessageBoxW(mainWindow_->GetHwnd(), text.c_str(), L"カメラ一覧", MB_OK);
    });
    mainWindow_->SetOnNewSession([this] { StartNewSignalingSession(); });
    mainWindow_->SetOnRotationChanged([this](int degrees) { rotationDegrees_ = degrees; });
    mainWindow_->SetOnToggleTestPattern([this] { testPatternMode_ = !testPatternMode_.load(); mainWindow_->SetTestPatternStatus(testPatternMode_); });
    mainWindow_->SetOnTorchToggle([this](bool enabled) { rtcManager_->SendControlMessage(std::string("{\"type\":\"remote_control\",\"cmd\":\"torch\",\"enabled\":") + (enabled ? "true}" : "false}")); });
    mainWindow_->SetOnZoomChange([this](float zoom) { rtcManager_->SendControlMessage("{\"type\":\"remote_control\",\"cmd\":\"zoom\",\"value\":" + std::to_string(zoom) + "}"); });
    mainWindow_->SetOnSwitchCamera([this] { rtcManager_->SendControlMessage("{\"type\":\"remote_control\",\"cmd\":\"switch_camera\"}"); });
    isVideoWorkerRunning_ = isOutputRunning_ = true;
    videoThread_ = std::thread(&AppController::VideoWorkerProc, this);
    outputThread_ = std::thread(&AppController::TestPatternWorkerProc, this);
    mainWindow_->Show(SW_SHOWNORMAL); StartNewSignalingSession(); return true;
}
void AppController::StartNewSignalingSession() {
    if (shuttingDown_) return;
    isSignalingRunning_ = false; httpClient_->Cancel(); rtcManager_->CancelPending();
    if (signalingThread_.joinable()) signalingThread_.join();
    rtcManager_->Close(); // closes the callback gate before reusing the receiver
    const uint64_t generation = videoQueue_.reset(); audioRenderer_.Flush();
    { std::lock_guard lock(frameMutex_); latestNv12_.reset(); latestArrivalNs_ = 0; }
    SetEvent(videoReady_); isSignalingRunning_ = true;
    signalingThread_ = std::thread(&AppController::SignalingWorkerProc, this, generation);
}
void AppController::SignalingWorkerProc(uint64_t generation) {
    auto status = [this, generation](std::wstring text) { PostUi(generation, [this, text = std::move(text)] { mainWindow_->SetStatusText(text); }); };
    try {
        status(L"セッション作成中..."); auto session = httpClient_->CreateSession("windows-receiver");
        if (!session || !isSignalingRunning_) { status(L"セッション作成に失敗しました。URLと接続を確認してください。"); return; }
        PostUi(generation, [this, url = session->joinUrl] { mainWindow_->SetJoinUrl(url); });
        status(L"QRコードを読み取り、ブラウザで送信を開始してください。");
        const auto begin = std::chrono::steady_clock::now(); uint32_t interval = session->poll.initialIntervalMs;
        while (isSignalingRunning_) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
            if (elapsed >= session->poll.timeoutMs) { status(L"接続待機がタイムアウトしました。"); return; }
            auto offer = httpClient_->PollOffer(session->sessionId, session->receiverToken);
            if (offer && isSignalingRunning_) {
                if (!rtcManager_->Initialize(session->rtcConfiguration,
                    [this, generation](rtc_net::PeerState state) {
                        if (state == rtc_net::PeerState::Disconnected || state == rtc_net::PeerState::Failed || state == rtc_net::PeerState::Closed) {
                            videoQueue_.invalidate(); audioRenderer_.Flush();
                        }
                        PostUi(generation, [this, state] {
                            if (state == rtc_net::PeerState::Connected) {
                                mainWindow_->SetStatusText(L"WebRTC接続完了。受信を開始しました。");
                                mainWindow_->SetVirtualCameraStatus(vcamRegistrar_.StartVirtualCamera());
                            } else if (state == rtc_net::PeerState::Disconnected || state == rtc_net::PeerState::Failed) mainWindow_->SetStatusText(L"WebRTC接続が切断されました。");
                        });
                    },
                    [this, generation](const uint8_t* data, size_t size, int, int, int64_t timestampUs) {
                        if (!data || !isVideoWorkerRunning_) return;
                        if (!videoQueue_.push({data, size}, timestampUs, generation)) rtcManager_->RequestKeyframe();
                        SetEvent(videoReady_);
                    },
                    [this](const int16_t* pcm, size_t elements, int channels, int rate) {
                        if (pcm && rate == 48000 && (channels == 1 || channels == 2) && elements % size_t(channels) == 0)
                            audioRenderer_.RenderPcm16({pcm, elements}, channels); // element count, NOT elements*channels
                    })) { status(L"WebRTC初期化失敗。ICE/TURN設定とビルドの対応範囲を確認してください。"); return; }
                rtcManager_->SetControlMessageCallback([this, generation](const std::string& text) {
                    try {
                        const auto root = km::json::parse(text);
                        if (root.at("type").string() != "camera_caps") return;
                        const auto* torch = root.find("supportsTorch");
                        const bool enabled = torch && torch->kind == km::json::Value::Kind::Bool && torch->text == "true";
                        const auto* facingValue = root.find("facingMode");
                        const std::string facing = facingValue && facingValue->string() == "user" ? "user" : "environment";
                        PostUi(generation, [this, enabled, facing] { mainWindow_->UpdateCameraCapabilities(enabled, 1, 5, 1, facing); });
                    } catch (const std::exception&) {}
                });
                std::string answer; status(L"WebRTC Answerを生成しています...");
                if (!rtcManager_->ProcessOfferAndGenerateAnswer(offer->sdp, answer) || !isSignalingRunning_) return;
                if (!httpClient_->PutAnswer(session->sessionId, session->receiverToken, answer)) status(L"Answer送信に失敗しました。");
                return;
            }
            if (elapsed > session->poll.backoffAfterMs) interval = std::min(interval + 500, session->poll.maxIntervalMs);
            for (uint32_t slept = 0; slept < interval && isSignalingRunning_; slept += 20) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    } catch (const std::exception&) { status(L"シグナリング処理に失敗しました。"); }
}
void AppController::VideoWorkerProc() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) { PostUi(0, [this] { mainWindow_->SetStatusText(L"映像処理スレッドの初期化に失敗しました。"); }); return; }
    DWORD task = 0; HANDLE priority = AvSetMmThreadCharacteristicsW(L"Capture", &task);
    {
        codec::H264Decoder decoder; media::Nv12Converter converter; // decoder is owned ONLY by this thread
        uint64_t activeGeneration = 0, activeReset = 0; unsigned noOutput = 0;
        std::vector<uint8_t> decoded; km::QueuedAccessUnit unit;
        while (isVideoWorkerRunning_) {
            WaitForSingleObject(videoReady_, 100);
            while (isVideoWorkerRunning_ && videoQueue_.pop(unit)) {
                if (!videoQueue_.current(unit)) continue;
                if (activeGeneration != unit.generation || activeReset != unit.resetSerial) {
                    decoder.Shutdown();
                    if (!decoder.Initialize(1280, 720, pipePublisher_.GetD3D11Device())) {
                        videoQueue_.invalidate(); rtcManager_->RequestKeyframe(); continue;
                    }
                    activeGeneration = unit.generation; activeReset = unit.resetSerial; noOutput = 0;
                }
                int width = 0, height = 0;
                if (!decoder.DecodeAccessUnit(unit.bytes.data(), unit.bytes.size(), unit.timestampUs, decoded, width, height)) {
                    if (++noOutput >= 60) { videoQueue_.invalidate(); rtcManager_->RequestKeyframe(); noOutput = 0; }
                    continue; // NEED_MORE_INPUT is not automatically a decoder failure
                }
                noOutput = 0;
                if (width <= 0 || height <= 0 || width > 8192 || height > 8192 || (width & 1) || (height & 1) || decoded.size() < size_t(width) * size_t(height) * 3 / 2) {
                    videoQueue_.invalidate(); rtcManager_->RequestKeyframe(); continue;
                }
                auto output = std::make_shared<std::vector<uint8_t>>(protocol::kPayloadBytes);
                converter.ConvertNv12ToNv12Letterbox(decoded.data(), width, width, height, output->data(), 1280, 720, rotationDegrees_);
                if (!videoQueue_.current(unit)) continue;
                std::lock_guard lock(frameMutex_);
                latestNv12_ = std::move(output); latestArrivalNs_ = monoNs();
                latestIdentity_.generation = unit.generation; latestIdentity_.resetSerial = unit.resetSerial;
            }
        }
        decoder.Shutdown(); // all COM/media resources are released on their owner thread
    }
    if (priority) AvRevertMmThreadCharacteristics(priority);
    CoUninitialize();
}
void AppController::TestPatternWorkerProc() {
    media::TestPatternGenerator pattern; std::vector<uint8_t> idle(protocol::kPayloadBytes); uint64_t index = 0;
    km::RationalPacer pacer(30, 1, monoNs());
    while (isOutputRunning_) {
        uint64_t deadline = pacer.next(), now = monoNs();
        if (now > deadline && now - deadline > 500000000) { pacer = km::RationalPacer(30, 1, now); deadline = pacer.next(); }
        else while (now > deadline && now - deadline >= 33333334) deadline = pacer.next(); // skip missed slots, never burst catch-up
        const auto wake = std::chrono::steady_clock::time_point(std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::nanoseconds(deadline)));
        { std::unique_lock lock(outputWaitMutex_); outputWake_.wait_until(lock, wake, [this] { return !isOutputRunning_; }); }
        if (!isOutputRunning_) break;
        now = monoNs(); std::shared_ptr<const std::vector<uint8_t>> frame;
        { std::lock_guard lock(frameMutex_);
            if (!testPatternMode_ && latestNv12_ && now >= latestArrivalNs_ && now - latestArrivalNs_ <= 1000000000 && videoQueue_.current(latestIdentity_)) frame = latestNv12_;
        }
        const int64_t timestampUs = int64_t(deadline / 1000);
        if (!frame) pattern.GenerateFrame(idle, index, timestampUs);
        const auto& pixels = frame ? *frame : idle;
        pipePublisher_.PublishFrame(pixels.data(), protocol::kPayloadBytes, timestampUs);
        mainWindow_->RenderPreviewFrame(pixels); ++index;
    }
}
void AppController::RunMessageLoop() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
}
void AppController::Shutdown() {
    if (shuttingDown_.exchange(true)) return;
    acceptingUi_ = false; isSignalingRunning_ = false;
    if (httpClient_) httpClient_->Cancel(); if (rtcManager_) rtcManager_->CancelPending();
    if (signalingThread_.joinable()) signalingThread_.join();
    if (rtcManager_) rtcManager_->Close(); // barrier first; no new media reaches the queues
    videoQueue_.reset(); isVideoWorkerRunning_ = false; if (videoReady_) SetEvent(videoReady_);
    if (videoThread_.joinable()) videoThread_.join();
    isOutputRunning_ = false; outputWake_.notify_all(); if (outputThread_.joinable()) outputThread_.join();
    audioRenderer_.Stop(); vcamRegistrar_.StopVirtualCamera(); pipePublisher_.Stop();
    MSG message{};
    while (uiDispatchWindow_ && uiThread_ == GetCurrentThreadId() && PeekMessageW(&message, uiDispatchWindow_, kUiWorkMessage, kUiWorkMessage, PM_REMOVE)) {
        delete reinterpret_cast<UiWork*>(message.lParam); --queuedUi_;
    }
    if (uiDispatchWindow_) { DestroyWindow(uiDispatchWindow_); uiDispatchWindow_ = nullptr; }
}
} // namespace km::app
