#include "wasapi_audio_renderer.h"

namespace km::audio {

WasapiAudioRenderer::WasapiAudioRenderer() {
    InitializeCriticalSectionAndSpinCount(&cs_, 4000);
}

WasapiAudioRenderer::~WasapiAudioRenderer() {
    Stop();
    DeleteCriticalSection(&cs_);
}

bool WasapiAudioRenderer::Initialize(const std::wstring& endpointId) {
    EnterCriticalSection(&cs_);
    Stop();

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) { LeaveCriticalSection(&cs_); return false; }

    Microsoft::WRL::ComPtr<IMMDevice> device;
    if (endpointId.empty()) {
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    } else {
        hr = enumerator->GetDevice(endpointId.c_str(), &device);
    }
    if (FAILED(hr) || !device) { LeaveCriticalSection(&cs_); return false; }

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &audioClient_);
    if (FAILED(hr) || !audioClient_) { LeaveCriticalSection(&cs_); return false; }

    // Configure 48kHz stereo 16-bit PCM format
    waveFormat_.wFormatTag = WAVE_FORMAT_PCM;
    waveFormat_.nChannels = 2;
    waveFormat_.nSamplesPerSec = 48000;
    waveFormat_.wBitsPerSample = 16;
    waveFormat_.nBlockAlign = (waveFormat_.nChannels * waveFormat_.wBitsPerSample) / 8;
    waveFormat_.nAvgBytesPerSec = waveFormat_.nSamplesPerSec * waveFormat_.nBlockAlign;
    waveFormat_.cbSize = 0;

    // Buffer duration: 10ms (100,000 in 100ns units) for sub-10ms ultra-low latency audio & tight A/V sync
    REFERENCE_TIME bufferDuration = 100000;
    hr = audioClient_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        0,
        bufferDuration,
        0,
        &waveFormat_,
        nullptr
    );

    if (FAILED(hr)) {
        // If exact PCM format is not supported, query mix format and initialize
        WAVEFORMATEX* mixFormat = nullptr;
        if (SUCCEEDED(audioClient_->GetMixFormat(&mixFormat)) && mixFormat) {
            hr = audioClient_->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, bufferDuration, 0, mixFormat, nullptr);
            if (SUCCEEDED(hr)) {
                waveFormat_ = *mixFormat;
            }
            CoTaskMemFree(mixFormat);
        }
    }
    if (FAILED(hr)) { LeaveCriticalSection(&cs_); return false; }

    hr = audioClient_->GetBufferSize(&bufferFrameCount_);
    if (FAILED(hr)) { LeaveCriticalSection(&cs_); return false; }

    hr = audioClient_->GetService(__uuidof(IAudioRenderClient), &renderClient_);
    LeaveCriticalSection(&cs_);
    return SUCCEEDED(hr);
}

void WasapiAudioRenderer::Start() {
    EnterCriticalSection(&cs_);
    if (audioClient_ && !isRunning_) {
        audioClient_->Start();
        isRunning_ = true;
    }
    LeaveCriticalSection(&cs_);
}

void WasapiAudioRenderer::Stop() {
    // Note: cs_ may already be held by Initialize() which calls Stop() recursively
    EnterCriticalSection(&cs_);
    if (audioClient_ && isRunning_) {
        audioClient_->Stop();
        isRunning_ = false;
    }
    renderClient_.Reset();
    audioClient_.Reset();
    LeaveCriticalSection(&cs_);
}

void WasapiAudioRenderer::RenderPcm16(std::span<const int16_t> pcmSamples, int channels) {
    if (!isRunning_ || !renderClient_ || !audioClient_ || pcmSamples.empty() || channels <= 0) return;

    EnterCriticalSection(&cs_);
    if (!renderClient_) { LeaveCriticalSection(&cs_); return; }

    UINT32 padding = 0;
    if (FAILED(audioClient_->GetCurrentPadding(&padding))) { LeaveCriticalSection(&cs_); return; }

    UINT32 availableFrames = (bufferFrameCount_ > padding) ? (bufferFrameCount_ - padding) : 0;
    UINT32 inputFrames = static_cast<UINT32>(pcmSamples.size() / channels);
    UINT32 framesToWrite = (std::min)(availableFrames, inputFrames);
    if (framesToWrite == 0) { LeaveCriticalSection(&cs_); return; }

    BYTE* pData = nullptr;
    if (FAILED(renderClient_->GetBuffer(framesToWrite, &pData)) || !pData) { LeaveCriticalSection(&cs_); return; }

    if (waveFormat_.wFormatTag == WAVE_FORMAT_PCM && waveFormat_.wBitsPerSample == 16 && waveFormat_.nChannels == 2) {
        if (channels == 2) {
            std::memcpy(pData, pcmSamples.data(), framesToWrite * sizeof(int16_t) * 2);
        } else if (channels == 1) {
            int16_t* dst = reinterpret_cast<int16_t*>(pData);
            for (UINT32 i = 0; i < framesToWrite; ++i) {
                dst[i * 2] = pcmSamples[i];
                dst[i * 2 + 1] = pcmSamples[i];
            }
        }
    } else if (waveFormat_.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        float* dst = reinterpret_cast<float*>(pData);
        for (UINT32 i = 0; i < framesToWrite; ++i) {
            float sampleL = static_cast<float>(pcmSamples[i * channels]) / 32768.0f;
            float sampleR = (channels > 1) ? (static_cast<float>(pcmSamples[i * channels + 1]) / 32768.0f) : sampleL;
            dst[i * 2] = sampleL;
            dst[i * 2 + 1] = sampleR;
        }
    } else {
        std::memset(pData, 0, framesToWrite * waveFormat_.nBlockAlign);
    }

    renderClient_->ReleaseBuffer(framesToWrite, 0);
    LeaveCriticalSection(&cs_);
}

} // namespace km::audio
