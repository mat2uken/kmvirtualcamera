#include "app_controller.h"
#include <chrono>

namespace km::app {

AppController::AppController() {
    nv12Buffer_.resize(protocol::kPayloadBytes);
    protocol::FillBlackNv12(nv12Buffer_);
}

AppController::~AppController() {
    Shutdown();
}

bool AppController::Initialize(HINSTANCE hInstance, std::wstring baseUrl) {
    baseUrl_ = baseUrl;
    httpClient_ = std::make_unique<signaling::WinHttpClient>(baseUrl_);
    rtcManager_ = std::make_unique<rtc_net::PeerConnectionManager>();

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

    if (rtcManager_) {
        rtcManager_->Close();
    }

    isSignalingRunning_ = true;
    signalingThread_ = std::thread(&AppController::SignalingWorkerProc, this);
}

void AppController::SignalingWorkerProc() {
    mainWindow_->SetStatusText(L"セッション作成中 (Cloudflare HTTPS API)...");

    auto sessionOpt = httpClient_->CreateSession("windows-receiver");
    if (!sessionOpt.has_value() || !isSignalingRunning_) {
        mainWindow_->SetStatusText(L"セッション作成失敗。URLと接続を確認してください。");
        return;
    }

    auto session = sessionOpt.value();
    mainWindow_->SetJoinUrl(session.joinUrl);
    mainWindow_->SetStatusText(L"QRコードをスマホで読み取り「送信開始」を押してください");

    // Poll for Offer SDP
    auto startTime = std::chrono::steady_clock::now();
    uint32_t intervalMs = session.poll.initialIntervalMs;

    while (isSignalingRunning_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
        if (elapsed > static_cast<int64_t>(session.poll.timeoutMs)) {
            mainWindow_->SetStatusText(L"タイムアウト: 送信側からのOfferがありませんでした。");
            return;
        }

        auto offerOpt = httpClient_->PollOffer(session.sessionId, session.receiverToken);
        if (offerOpt.has_value()) {
            mainWindow_->SetStatusText(L"Offer受信。WebRTC Answer生成中 (libdatachannel)...");

            bool ok = rtcManager_->Initialize(
                session.rtcConfiguration,
                [this](rtc_net::PeerState state) {
                    if (state == rtc_net::PeerState::Connected) {
                        mainWindow_->SetStatusText(L"WebRTC接続完了 (映像・音声受信中)");
                        vcamRegistrar_.StartVirtualCamera();
                        mainWindow_->SetVirtualCameraStatus(true);
                    } else if (state == rtc_net::PeerState::Disconnected || state == rtc_net::PeerState::Failed) {
                        mainWindow_->SetStatusText(L"WebRTC切断");
                    }
                },
                [this](const uint8_t* data, size_t size, int width, int height, int64_t tsUs) {
                    std::lock_guard<std::mutex> lock(videoProcessMutex_);
                    // Convert frame to 1280x720 NV12 with letterbox
                    nv12Converter_.ConvertI420ToNv12(
                        data, width,
                        data + (width * height), width / 2,
                        data + (width * height) + ((width * height) / 4), width / 2,
                        width, height,
                        nv12Buffer_.data(), 1280, 720
                    );

                    mainWindow_->RenderPreviewFrame(nv12Buffer_);
                    pipePublisher_.PublishFrame(nv12Buffer_.data(), protocol::kPayloadBytes, tsUs);
                },
                [this](const int16_t* pcm, size_t samples, int channels, int sampleRate) {
                    audioRenderer_.RenderPcm16(std::span<const int16_t>(pcm, samples * channels), channels);
                }
            );

            if (!ok) {
                mainWindow_->SetStatusText(L"WebRTC初期化失敗");
                return;
            }

            std::string answerSdp;
            if (!rtcManager_->ProcessOfferAndGenerateAnswer(offerOpt->sdp, answerSdp) || !isSignalingRunning_) {
                mainWindow_->SetStatusText(L"Answer生成またはICE収集に失敗しました。");
                return;
            }

            mainWindow_->SetStatusText(L"Answer送信中...");
            if (!httpClient_->PutAnswer(session.sessionId, session.receiverToken, answerSdp)) {
                mainWindow_->SetStatusText(L"Answer送信に失敗しました。");
                return;
            }

            mainWindow_->SetStatusText(L"接続待機中...");
            break;
        }

        if (elapsed > static_cast<int64_t>(session.poll.backoffAfterMs)) {
            intervalMs = (std::min)(intervalMs + 500, session.poll.maxIntervalMs);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
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

    vcamRegistrar_.StopVirtualCamera();
    pipePublisher_.Stop();
    audioRenderer_.Stop();

    if (rtcManager_) {
        rtcManager_->Close();
    }
}

} // namespace km::app
