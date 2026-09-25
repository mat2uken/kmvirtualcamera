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
    std::string base8; for (wchar_t c : baseUrl) base8 += (c >= 0x20 && c < 0x7F) ? char(c) : '?';
    std::cout << "[APP] initialize begin baseUrl=" << base8 << "\n";
    uiThread_ = GetCurrentThreadId(); MSG ignored{}; PeekMessageW(&ignored, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    httpClient_ = std::make_unique<signaling::WinHttpClient>(std::move(baseUrl));
    rtcManager_ = std::make_unique<rtc_net::PeerConnectionManager>();
    mainWindow_ = std::make_unique<ui::MainWindow>(); if (!mainWindow_->Create(instance)) { std::cerr << "[APP] main window create failed\n"; return false; }
    WNDCLASSW dispatchClass{};
    dispatchClass.hInstance = instance; dispatchClass.lpfnWndProc = &AppController::UiDispatchProc;
    dispatchClass.lpszClassName = L"KMVirtualCamera.UiDispatch";
    if (!RegisterClassW(&dispatchClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { std::cerr << "[APP] ui dispatch class registration failed err=" << GetLastError() << "\n"; return false; }
    uiDispatchWindow_ = CreateWindowExW(0, dispatchClass.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
    if (!uiDispatchWindow_) { std::cerr << "[APP] ui dispatch window create failed\n"; return false; }
    acceptingUi_ = true;
    std::cout << "[APP] windows created\n";
    audioDevices_ = audioEnumerator_.EnumerateRenderDevices();
    int selected = 0;
    for (size_t i = 0; i < audioDevices_.size(); ++i) if (audioDevices_[i].isCableInput) { selected = int(i); break; }
    const bool audioStarted = !audioDevices_.empty() && audioRenderer_.Initialize(audioDevices_[size_t(selected)].id);
    if (audioStarted) audioRenderer_.Start();
    std::cout << "[APP] audio devices=" << audioDevices_.size() << " selected=" << selected
              << " renderer=" << (audioStarted ? "on" : "off") << "\n";
    mainWindow_->SetAudioDevices(audioDevices_, selected);
    mainWindow_->SetOnAudioDeviceChanged([this](int index) {
        if (index >= 0 && size_t(index) < audioDevices_.size() && audioRenderer_.Initialize(audioDevices_[size_t(index)].id)) audioRenderer_.Start();
    });
    pipePublisher_.Start();
    std::cout << "[APP] pipe publisher started\n";
    const bool registered = vcam::VirtualCameraRegistrar::IsVirtualCameraRegistered();
    mainWindow_->SetVirtualCameraRegistered(registered);
    const bool vcamStarted = vcamRegistrar_.StartVirtualCamera(L"WebRTC Bridge Virtual Camera");
    mainWindow_->SetVirtualCameraStatus(vcamStarted);
    std::cout << "[APP] vcam registered=" << (registered ? 1 : 0) << " started=" << (vcamStarted ? 1 : 0) << "\n";
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
    mainWindow_->Show(SW_SHOWNORMAL);
    std::cout << "[APP] initialize complete; starting signaling session\n";
    StartNewSignalingSession(); return true;
}
void AppController::StartNewSignalingSession() {
    if (shuttingDown_) return;
    isSignalingRunning_ = false; httpClient_->Cancel(); rtcManager_->CancelPending();
    if (signalingThread_.joinable()) signalingThread_.join();
    rtcManager_->Close(); // closes the callback gate before reusing the receiver
    const uint64_t generation = videoQueue_.reset(); audioRenderer_.Flush();
    { std::lock_guard lock(frameMutex_); latestNv12_.reset(); latestArrivalNs_ = 0; }
    SetEvent(videoReady_); isSignalingRunning_ = true;
    std::cout << "[SIG] session reset generation=" << generation << "\n";
    signalingThread_ = std::thread(&AppController::SignalingWorkerProc, this, generation);
}
void AppController::SignalingWorkerProc(uint64_t generation) {
    auto status = [this, generation](std::wstring text) { PostUi(generation, [this, text = std::move(text)] { mainWindow_->SetStatusText(text); }); };
    try {
        status(L"セッション作成中..."); auto session = httpClient_->CreateSession("windows-receiver");
        if (!session || !isSignalingRunning_) {
            std::cerr << "[SIG] create session failed running=" << (isSignalingRunning_ ? 1 : 0) << "\n";
            status(L"セッション作成に失敗しました。URLと接続を確認してください。"); return;
        }
        std::cout << "[SIG] session created join=" << session->joinUrl
                  << " pollTimeoutMs=" << session->poll.timeoutMs << "\n";
        PostUi(generation, [this, url = session->joinUrl] { mainWindow_->SetJoinUrl(url); });
        status(L"QRコードを読み取り、ブラウザで送信を開始してください。");
        const auto begin = std::chrono::steady_clock::now(); uint32_t interval = session->poll.initialIntervalMs;
        while (isSignalingRunning_) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
            if (elapsed >= session->poll.timeoutMs) { std::cerr << "[SIG] offer wait timeout elapsedMs=" << elapsed << "\n"; status(L"接続待機がタイムアウトしました。"); return; }
            auto offer = httpClient_->PollOffer(session->sessionId, session->receiverToken);
            if (offer && isSignalingRunning_) {
                std::cout << "[SIG] offer received bytes=" << offer->sdp.size() << "\n";
                if (!rtcManager_->Initialize(session->rtcConfiguration,
                    [this, generation](rtc_net::PeerState state) {
                        std::cout << "[RTC] state=" << int(state) << "\n";
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
                        static std::atomic<uint64_t> frames{0}, bytes{0};
                        const uint64_t framesNow = ++frames, bytesNow = (bytes += size);
                        if (framesNow % 90 == 0) std::cout << "[MEDIA] video frames=" << framesNow << " bytes=" << bytesNow << " gen=" << generation << "\n";
                        if (!videoQueue_.push({data, size}, timestampUs, generation)) rtcManager_->RequestKeyframe();
                        SetEvent(videoReady_);
                    },
                    [this](const int16_t* pcm, size_t elements, int channels, int rate) {
                        if (!pcm || rate != 48000 || (channels != 1 && channels != 2) || elements % size_t(channels) != 0) {
                            static std::atomic<uint64_t> dropped{0};
                            const uint64_t n = ++dropped; if (n == 1 || n % 250 == 0) std::cerr << "[MEDIA] audio callback dropped=" << n << " rate=" << rate << " ch=" << channels << "\n";
                            return;
                        }
                        static std::atomic<uint64_t> rendered{0}, samples{0};
                        const uint64_t calls = ++rendered, samplesNow = (samples += elements);
                        if (calls % 250 == 0) std::cout << "[MEDIA] audio callbacks=" << calls << " samples=" << samplesNow << "\n";
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
                if (!rtcManager_->ProcessOfferAndGenerateAnswer(offer->sdp, answer) || !isSignalingRunning_) {
                    std::cerr << "[SIG] answer generation failed or session cancelled\n"; return;
                }
                if (!httpClient_->PutAnswer(session->sessionId, session->receiverToken, answer)) {
                    std::cerr << "[SIG] PutAnswer failed answerBytes=" << answer.size() << "\n";
                    status(L"Answer送信に失敗しました。");
                } else std::cout << "[SIG] answer sent bytes=" << answer.size() << "\n";
                return;
            }
            if (elapsed > session->poll.backoffAfterMs) interval = std::min(interval + 500, session->poll.maxIntervalMs);
            for (uint32_t slept = 0; slept < interval && isSignalingRunning_; slept += 20) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    } catch (const std::exception& e) { std::cerr << "[SIG] worker exception: " << e.what() << "\n"; status(L"シグナリング処理に失敗しました。"); }
}
void AppController::VideoWorkerProc() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) { PostUi(0, [this] { mainWindow_->SetStatusText(L"映像処理スレッドの初期化に失敗しました。"); }); return; }
    DWORD task = 0; HANDLE priority = AvSetMmThreadCharacteristicsW(L"Capture", &task);
    {
        codec::H264Decoder decoder; media::Nv12Converter converter; // decoder is owned ONLY by this thread
        uint64_t activeGeneration = 0, activeReset = 0; unsigned noOutput = 0;
        uint64_t decodedCount = 0;
        std::vector<uint8_t> decoded; km::QueuedAccessUnit unit;
        while (isVideoWorkerRunning_) {
            videoWorkerStep_ = 80;
            WaitForSingleObject(videoReady_, 100);
            videoWorkerStep_ = 81;
            while (isVideoWorkerRunning_) {
                videoWorkerStep_ = 70;
                const bool gotUnit = videoQueue_.pop(unit);
                videoWorkerStep_ = 71;
                if (!gotUnit) break;
                if (!videoQueue_.current(unit)) continue;
                if (activeGeneration != unit.generation || activeReset != unit.resetSerial) {
                    videoWorkerStep_ = 20;
                    const auto reinitBegan = GetTickCount64();
                    decoder.Shutdown();
                    videoWorkerStep_ = 22;
                    const bool reinitOk = decoder.Initialize(1280, 720, pipePublisher_.GetD3D11Device());
                    videoWorkerStep_ = 24;
                    const auto reinitMs = GetTickCount64() - reinitBegan;
                    if (reinitMs > 1000) std::cout << "[APP] video worker slow decoder reinit ms=" << reinitMs << "\n";
                    if (!reinitOk) {
                        videoQueue_.invalidate();
                        videoWorkerStep_ = 50; rtcManager_->RequestKeyframe(); videoWorkerStep_ = 51;
                        continue;
                    }
                    activeGeneration = unit.generation; activeReset = unit.resetSerial; noOutput = 0;
                }
                int width = 0, height = 0;
                const auto decodeBegan = GetTickCount64();
                videoWorkerStep_ = 30;
                const bool decodeOk = decoder.DecodeAccessUnit(unit.bytes.data(), unit.bytes.size(), unit.timestampUs, decoded, width, height);
                videoWorkerStep_ = 32;
                const auto decodeMs = GetTickCount64() - decodeBegan;
                if (decodeMs > 1000) std::cout << "[APP] video worker slow decode ms=" << decodeMs << "\n";
                if (!decodeOk) {
                    if (++noOutput >= 60) {
                        videoQueue_.invalidate();
                        videoWorkerStep_ = 50; rtcManager_->RequestKeyframe(); videoWorkerStep_ = 51;
                        noOutput = 0;
                    }
                    continue; // NEED_MORE_INPUT is not automatically a decoder failure
                }
                noOutput = 0;
                // Worker-side progress heartbeat: proves the decoder loop itself
                // (not just the push side) is alive through the stream.
                if (++decodedCount % 90 == 0) std::cout << "[APP] video worker decoded=" << decodedCount << "\n";
                if (width <= 0 || height <= 0 || width > 8192 || height > 8192 || (width & 1) || (height & 1) || decoded.size() < size_t(width) * size_t(height) * 3 / 2) {
                    videoQueue_.invalidate();
                    videoWorkerStep_ = 50; rtcManager_->RequestKeyframe(); videoWorkerStep_ = 51;
                    continue;
                }
                auto output = std::make_shared<std::vector<uint8_t>>(protocol::kPayloadBytes);
                videoWorkerStep_ = 60;
                converter.ConvertNv12ToNv12Letterbox(decoded.data(), width, width, height, output->data(), 1280, 720, rotationDegrees_);
                videoWorkerStep_ = 61;
                if (!videoQueue_.current(unit)) continue;
                videoWorkerStep_ = 40;
                {
                    std::lock_guard lock(frameMutex_);
                    videoWorkerStep_ = 41;
                    latestNv12_ = std::move(output); latestArrivalNs_ = monoNs();
                    latestIdentity_.generation = unit.generation; latestIdentity_.resetSerial = unit.resetSerial;
                }
                videoWorkerStep_ = 42;
            }
        }
        videoWorkerStep_ = 90;
        std::cout << "[APP] video worker loop exited\n";
        const auto releaseBegan = GetTickCount64();
        videoWorkerStep_ = 92;
        decoder.Shutdown(); // all COM/media resources are released on their owner thread
        videoWorkerStep_ = 94;
        std::cout << "[APP] video worker decoder released ms=" << (GetTickCount64() - releaseBegan) << "\n";
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
        outputWorkerStep_ = 40;
        { std::unique_lock lock(outputWaitMutex_); outputWake_.wait_until(lock, wake, [this] { return !isOutputRunning_; }); }
        outputWorkerStep_ = 42;
        if (!isOutputRunning_) break;
        now = monoNs(); std::shared_ptr<const std::vector<uint8_t>> frame;
        outputWorkerStep_ = 30;
        { std::lock_guard lock(frameMutex_);
            if (!testPatternMode_ && latestNv12_ && now >= latestArrivalNs_ && now - latestArrivalNs_ <= 1000000000 && videoQueue_.current(latestIdentity_)) frame = latestNv12_;
        }
        outputWorkerStep_ = 32;
        const int64_t timestampUs = int64_t(deadline / 1000);
        if (!frame) pattern.GenerateFrame(idle, index, timestampUs);
        const auto& pixels = frame ? *frame : idle;
        const auto frameBegan = GetTickCount64();
        outputWorkerStep_ = 20;
        pipePublisher_.PublishFrame(pixels.data(), protocol::kPayloadBytes, timestampUs);
        outputWorkerStep_ = 24;
        mainWindow_->RenderPreviewFrame(pixels); ++index;
        outputWorkerStep_ = 26;
        const auto frameMs = GetTickCount64() - frameBegan;
        if (frameMs > 1000) std::cout << "[APP] output worker slow frame ms=" << frameMs << "\n";
    }
    outputWorkerStep_ = 90;
    std::cout << "[APP] output worker loop exited\n";
}
void AppController::RunMessageLoop() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
}
void AppController::Shutdown() {
    if (shuttingDown_.exchange(true)) return;
    std::cout << "[APP] shutdown begin queuedUi=" << queuedUi_ << "\n";
    acceptingUi_ = false; isSignalingRunning_ = false;
    if (httpClient_) httpClient_->Cancel(); if (rtcManager_) rtcManager_->CancelPending();
    if (signalingThread_.joinable()) signalingThread_.join();
    std::cout << "[APP] shutdown stage signaling-joined\n";
    if (rtcManager_) rtcManager_->Close(); // barrier first; no new media reaches the queues
    std::cout << "[APP] shutdown stage rtc-closed\n";
    // Watchdog: while a worker join stalls, report where each worker is stuck.
    std::thread shutdownWatchdog([this] {
        for (int i = 0; i < 120 && !(videoWorkerJoined_ && outputWorkerJoined_); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (i % 20 == 19 && !(videoWorkerJoined_ && outputWorkerJoined_))
                std::cout << "[APP] shutdown watchdog: video step=" << videoWorkerStep_.load()
                          << " decoder stage=" << codec::H264Decoder::DebugStage().load()
                          << " output step=" << outputWorkerStep_.load() << "\n";
        }
    });
    videoQueue_.reset(); isVideoWorkerRunning_ = false; if (videoReady_) SetEvent(videoReady_);
    if (videoThread_.joinable()) videoThread_.join();
    videoWorkerJoined_ = true;
    std::cout << "[APP] shutdown stage video-joined\n";
    isOutputRunning_ = false; outputWake_.notify_all(); if (outputThread_.joinable()) outputThread_.join();
    outputWorkerJoined_ = true;
    std::cout << "[APP] shutdown stage output-joined\n";
    audioRenderer_.Stop(); vcamRegistrar_.StopVirtualCamera(); pipePublisher_.Stop();
    std::cout << "[APP] shutdown stage media-stopped\n";
    MSG message{};
    while (uiDispatchWindow_ && uiThread_ == GetCurrentThreadId() && PeekMessageW(&message, uiDispatchWindow_, kUiWorkMessage, kUiWorkMessage, PM_REMOVE)) {
        delete reinterpret_cast<UiWork*>(message.lParam); --queuedUi_;
    }
    if (uiDispatchWindow_) { DestroyWindow(uiDispatchWindow_); uiDispatchWindow_ = nullptr; }
    if (shutdownWatchdog.joinable()) shutdownWatchdog.join(); // exits within 500ms of both joins
    std::cout << "[APP] shutdown complete\n";
}
} // namespace km::app
