#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <vector>
#include <mutex>
#include <atomic>
#include <span>

namespace km::audio {

class WasapiAudioRenderer {
public:
    WasapiAudioRenderer();
    ~WasapiAudioRenderer();

    bool Initialize(const std::wstring& endpointId = L"");
    void Start();
    void Stop();

    // Renders 48kHz 16-bit stereo PCM audio to the WASAPI endpoint.
    void RenderPcm16(std::span<const int16_t> pcmSamples, int channels = 2);

private:
    std::atomic<bool> isRunning_{false};
    Microsoft::WRL::ComPtr<IAudioClient> audioClient_;
    Microsoft::WRL::ComPtr<IAudioRenderClient> renderClient_;
    UINT32 bufferFrameCount_{0};
    WAVEFORMATEX waveFormat_{};
    std::recursive_mutex renderMutex_;
};

} // namespace km::audio
