#include "app_controller.h"
#include <chrono>

namespace km::app {

AppController::AppController() {
    hVideoFrameReadyEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

AppController::~AppController() {
    Shutdown();
    if (hVideoFrameReadyEvent_) {
        CloseHandle(hVideoFrameReadyEvent_);
        hVideoFrameReadyEvent_ = nullptr;
    }
}

bool AppController::Initialize(HINSTANCE hInstance, std::wstring baseUrl) {
    baseUrl_ = baseUrl;
    httpClient_ = std::make_unique<signaling::WinHttpClient>(baseUrl_);
    rtcManager_ = std::make_unique<rtc_net::PeerConnectionManager>();

    isVideoWorkerRunning_ = true;
    videoWorkerThread_ = std::thread(&AppController::VideoWorkerProc, this);

    // 1. Enumerate Audio endpoints
    audioDevices_ = audioEnumerator_.EnumerateRenderDevices();
    selectedAudioIndex_ = 0;
    for (size_t i = 0; i < audioDevices_.size(); ++i) {
        if (audioDevices_[i].isCableInput) {
            selectedAudioIndex_ = static_cast<int>(i);
            break;
        }
    }

    if (!audioDevices_.empty()) {
        audioRenderer_.Initialize(audioDevices_[selectedAudioIndex_].id);
        audioRenderer_.Start();
    }

    // 2. Start Named Pipe Publisher
    pipePublisher_.Start();

    // 3. Create Main Window
    mainWindow_ = std::make_unique<ui::MainWindow>();
    if (!mainWindow_->Create(hInstance)) {
        return false;
    }

    mainWindow_->SetAudioDevices(audioDevices_, selectedAudioIndex_);
    mainWindow_->SetOnAudioDeviceChanged([this](int idx) {
        if (idx >= 0 && idx < static_cast<int>(audioDevices_.size())) {
            selectedAudioIndex_ = idx;
            audioRenderer_.Initialize(audioDevices_[idx].id);
            audioRenderer_.Start();
        }
    });

    mainWindow_->SetOnToggleVirtualCamera([this]() {
        if (vcamRegistrar_.IsRunning()) {
            vcamRegistrar_.StopVirtualCamera();
            mainWindow_->SetVirtualCameraStatus(false);
        } else {
            bool ok = vcamRegistrar_.StartVirtualCamera();
            mainWindow_->SetVirtualCameraStatus(ok);
        }
    });

    mainWindow_->SetOnNewSession([this]() {
        StartNewSignalingSession();
    });

    mainWindow_->SetOnRotationChanged([this](int deg) {
        rotationDegrees_.store(deg);
    });

    mainWindow_->Show(SW_SHOWNORMAL);

    // 4. Start Signaling Session
    StartNewSignalingSession();
    return true;
}

void AppController::StartNewSignalingSession() {
    if (isSignalingRunning_.exchange(false)) {
        if (signalingThread_.joinable()) {
            signalingThread_.join();
        }
    }

    h264Decoder_.Shutdown();

    if (rtcManager_) {
        rtcManager_->Close();
    }

    isSignalingRunning_ = true;
    signalingThread_ = std::thread(&AppController::SignalingWorkerProc, this);
}

void AppController::SignalingWorkerProc() {
    mainWindow_->SetStatusText(L"セッション作成中 (Cloudflare HTTPS API)...");
    std::string baseUrlNarrow;
    for (wchar_t c : baseUrl_) { baseUrlNarrow += static_cast<char>(c); }
    std::cout << "[Signaling] Creating session on " << baseUrlNarrow << "..." << std::endl;

    auto sessionOpt = httpClient_->CreateSession("windows-receiver");
    if (!sessionOpt.has_value() || !isSignalingRunning_) {
        mainWindow_->SetStatusText(L"セッション作成失敗。URLと接続を確認してください。");
        std::cout << "[Signaling] ERROR: Failed to create session." << std::endl;
        return;
    }

    auto session = sessionOpt.value();
    mainWindow_->SetJoinUrl(session.joinUrl);
    mainWindow_->SetStatusText(L"QRコードをスマホで読み取り「送信開始」を押してください");
    std::cout << "[Signaling] Session ready. SessionID=" << session.sessionId << ", JoinUrl=" << session.joinUrl << std::endl;

    // Poll for Offer SDP
    auto startTime = std::chrono::steady_clock::now();
    uint32_t intervalMs = session.poll.initialIntervalMs;

    while (isSignalingRunning_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
        if (elapsed > static_cast<int64_t>(session.poll.timeoutMs)) {
            mainWindow_->SetStatusText(L"タイムアウトしました。「新しいセッション」を押して再試行してください。");
            std::cout << "[Signaling] Timeout waiting for Offer after " << elapsed << " ms." << std::endl;
            return;
        }

        auto offerOpt = httpClient_->PollOffer(session.sessionId, session.receiverToken);
        if (offerOpt.has_value()) {
            mainWindow_->SetStatusText(L"Offer受信。WebRTC Answer生成中 (libdatachannel)...");
            std::cout << "[Signaling] Offer received (" << offerOpt->sdp.length() << " chars). Initializing WebRTC..." << std::endl;

            h264Decoder_.Initialize(1280, 720);

            bool ok = rtcManager_->Initialize(
                session.rtcConfiguration,
                [this](rtc_net::PeerState state) {
                    if (state == rtc_net::PeerState::Connected) {
                        std::cout << "[WebRTC] PeerState: Connected! Video/Audio streaming active." << std::endl;
                        mainWindow_->SetStatusText(L"WebRTC接続完了 (映像・音声受信中)");
                        vcamRegistrar_.StartVirtualCamera();
                        mainWindow_->SetVirtualCameraStatus(true);
                    } else if (state == rtc_net::PeerState::Disconnected || state == rtc_net::PeerState::Failed) {
                        std::cout << "[WebRTC] PeerState: Disconnected/Failed." << std::endl;
                        mainWindow_->SetStatusText(L"WebRTC切断");
                    }
                },
                [this](const uint8_t* data, size_t size, int width, int height, int64_t tsUs) {
                    if (!data || size == 0 || !isVideoWorkerRunning_) return;
                    lockFreeVideoQueue_.Push(data, size, tsUs);
                    SetEvent(hVideoFrameReadyEvent_);
                },
                [this](const int16_t* pcm, size_t samples, int channels, int sampleRate) {
                    audioRenderer_.RenderPcm16(std::span<const int16_t>(pcm, samples * channels), channels);
                }
            );

            if (!ok) {
                mainWindow_->SetStatusText(L"WebRTC初期化失敗");
                std::cout << "[WebRTC] ERROR: rtcManager_->Initialize failed." << std::endl;
                return;
            }

            std::string answerSdp;
            std::cout << "[WebRTC] Generating Answer SDP and gathering ICE candidates..." << std::endl;
            if (!rtcManager_->ProcessOfferAndGenerateAnswer(offerOpt->sdp, answerSdp) || !isSignalingRunning_) {
                mainWindow_->SetStatusText(L"Answer生成またはICE収集に失敗しました。");
                std::cout << "[WebRTC] ERROR: ProcessOfferAndGenerateAnswer failed." << std::endl;
                return;
            }

            mainWindow_->SetStatusText(L"Answer送信中...");
            std::cout << "[Signaling] Sending Answer SDP (" << answerSdp.length() << " chars) to server..." << std::endl;
            if (!httpClient_->PutAnswer(session.sessionId, session.receiverToken, answerSdp)) {
                mainWindow_->SetStatusText(L"Answer送信に失敗しました。");
                std::cout << "[Signaling] ERROR: PutAnswer failed." << std::endl;
                return;
            }

            mainWindow_->SetStatusText(L"接続待機中...");
            std::cout << "[Signaling] Answer sent successfully. Waiting for ICE/DTLS handshake..." << std::endl;
            break;
        }

        if (elapsed > static_cast<int64_t>(session.poll.backoffAfterMs)) {
            intervalMs = (std::min)(intervalMs + 500, session.poll.maxIntervalMs);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
    }
}

void AppController::VideoWorkerProc() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    std::vector<uint8_t> localH264Buffer;
    std::vector<uint8_t> localDecodedBuffer;
    std::vector<uint8_t> localNv12Buffer(protocol::kPayloadBytes);
    localH264Buffer.reserve(256 * 1024);

    while (isVideoWorkerRunning_) {
        WaitForSingleObject(hVideoFrameReadyEvent_, 100);
        if (!isVideoWorkerRunning_) break;

        // Drain all available frames from lock-free queue
        int64_t tsUs = 0;
        while (lockFreeVideoQueue_.Pop(localH264Buffer, tsUs)) {
            int decW = 0, decH = 0;
            if (h264Decoder_.DecodeAccessUnit(localH264Buffer.data(), localH264Buffer.size(), tsUs, localDecodedBuffer, decW, decH)) {
                nv12Converter_.ConvertNv12ToNv12Letterbox(
                    localDecodedBuffer.data(), decW,
                    decW, decH,
                    localNv12Buffer.data(),
                    1280, 720,
                    rotationDegrees_.load(std::memory_order_relaxed)
                );

                mainWindow_->RenderPreviewFrame(localNv12Buffer);
                pipePublisher_.PublishFrame(localNv12Buffer.data(), protocol::kPayloadBytes, tsUs);

                uint64_t count = frameCount_.fetch_add(1, std::memory_order_relaxed) + 1;
                if (count % 90 == 1) {
                    std::cout << "[VideoPipeline] Stream active: " << count << " frames decoded (" << decW << "x" << decH << " -> 1280x720)" << std::endl;
                }
            }
        }
    }
}

void AppController::RunMessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void AppController::Shutdown() {
    isSignalingRunning_ = false;
    if (signalingThread_.joinable()) {
        signalingThread_.join();
    }

    isVideoWorkerRunning_ = false;
    SetEvent(hVideoFrameReadyEvent_);
    if (videoWorkerThread_.joinable()) {
        videoWorkerThread_.join();
    }

    vcamRegistrar_.StopVirtualCamera();
    pipePublisher_.Stop();
    audioRenderer_.Stop();

    if (rtcManager_) {
        rtcManager_->Close();
    }
}

} // namespace km::app
