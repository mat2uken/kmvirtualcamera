#include <windows.h>
#include <mfapi.h>
#include <iostream>
#include "../receiver/app/app_controller.h"
#include "../receiver/ui/main_window.h"
#include "../receiver/audio/audio_device_enumerator.h"
#include "../receiver/audio/wasapi_audio_renderer.h"
#include "../receiver/media/pipe_publisher.h"

int main() {
    std::cout << "[STEP 1] CoInitializeEx..." << std::endl;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    std::cout << "[STEP 2] MFStartup..." << std::endl;
    MFStartup(MF_VERSION);

    HINSTANCE hInstance = GetModuleHandle(nullptr);

    try {
        std::cout << "[STEP 3] AudioDeviceEnumerator..." << std::endl;
        km::audio::AudioDeviceEnumerator enumerator;
        auto devices = enumerator.EnumerateRenderDevices();
        std::cout << "  Found " << devices.size() << " audio devices." << std::endl;

        std::cout << "[STEP 4] WasapiAudioRenderer..." << std::endl;
        km::audio::WasapiAudioRenderer audioRenderer;
        if (!devices.empty()) {
            audioRenderer.Initialize(devices[0].id);
            audioRenderer.Start();
        }

        std::cout << "[STEP 5] PipePublisher..." << std::endl;
        km::media::PipePublisher pipePublisher;
        pipePublisher.Start();

        std::cout << "[STEP 6] MainWindow Create..." << std::endl;
        km::ui::MainWindow mainWindow;
        bool winOk = mainWindow.Create(hInstance);
        std::cout << "  MainWindow::Create -> " << (winOk ? "OK" : "FAIL") << std::endl;

        std::cout << "[STEP 7] MainWindow Show..." << std::endl;
        mainWindow.Show(SW_SHOWNORMAL);

        std::cout << "[STEP 8] RenderPreviewFrame Zero-Green Verification (0, 90, 180, 270 deg)..." << std::endl;
        km::media::Nv12Converter converter;
        std::vector<uint8_t> testSource(1280 * 720 * 3 / 2, 200);
        // Fill UV with valid skin/neutral color U=120, V=140
        memset(testSource.data() + (1280 * 720), 120, 1280 * 360 / 2);
        memset(testSource.data() + (1280 * 720) + (1280 * 360 / 2), 140, 1280 * 360 / 2);

        for (int angle : {0, 90, 180, 270}) {
            std::vector<uint8_t> outputNv12(km::protocol::kPayloadBytes);
            converter.ConvertNv12ToNv12Letterbox(
                testSource.data(), 1280,
                1280, 720,
                outputNv12.data(), 1280, 720,
                angle
            );

            // Render to preview window
            mainWindow.RenderPreviewFrame(outputNv12);

            // Verify that all UV bytes in outputNv12 are >= 16 (never 0x00 / green)
            const uint8_t* uv = outputNv12.data() + (1280 * 720);
            for (int i = 0; i < 1280 * 360; ++i) {
                if (uv[i] == 0) {
                    std::cerr << "FAIL: Green UV byte (0x00) detected at index " << i << " for angle " << angle << std::endl;
                    exit(1);
                }
            }
        }
        std::cout << "  [PASS] RenderPreviewFrame tested successfully (0 green pixels)." << std::endl;

        std::cout << "[STEP 9] AppController full test..." << std::endl;
        km::app::AppController controller;
        controller.Initialize(hInstance, L"https://webrtc-bridge-signaling.mat2uken.workers.dev");

        std::cout << "[STEP 10] Running message loop for 2 seconds..." << std::endl;
        auto start = GetTickCount64();
        MSG msg{};
        while (GetTickCount64() - start < 2000) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            Sleep(20);
        }

        std::cout << "[STEP 11] Shutdown..." << std::endl;
        controller.Shutdown();
        pipePublisher.Stop();
        audioRenderer.Stop();
    } catch (const std::exception& e) {
        std::cout << "[EXCEPTION] " << e.what() << std::endl;
    } catch (...) {
        std::cout << "[EXCEPTION] Unknown" << std::endl;
    }

    MFShutdown();
    CoUninitialize();
    std::cout << "[DONE]" << std::endl;
    return 0;
}
