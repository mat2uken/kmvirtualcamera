#include "wasapi_audio_renderer.h"
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
namespace km::audio {
struct WasapiAudioRenderer::Impl {
    static constexpr size_t kCapacity = 19200; // 200ms, 48kHz stereo
    std::array<int16_t, kCapacity> pcm{};
    size_t head = 0, count = 0;
    std::mutex mutex; std::condition_variable cv; std::thread worker;
    bool quit = false, ready = false, ok = false, run = false;
    void process(std::wstring endpoint) {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
        Microsoft::WRL::ComPtr<IMMDevice> device;
        Microsoft::WRL::ComPtr<IAudioClient> client;
        Microsoft::WRL::ComPtr<IAudioRenderClient> render;
        UINT32 capacity = 0;
        HRESULT hr = com;
        if (SUCCEEDED(hr)) hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        if (SUCCEEDED(hr)) hr = endpoint.empty() ? enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device) : enumerator->GetDevice(endpoint.c_str(), &device);
        if (SUCCEEDED(hr)) hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
        WAVEFORMATEX format{}; format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = 2;
        format.nSamplesPerSec = 48000; format.wBitsPerSample = 16; format.nBlockAlign = 4; format.nAvgBytesPerSec = 192000;
        // Keep the application-side format fixed. Let WASAPI convert the endpoint
        // format/rate; never write stereo samples into an arbitrary mix-format buffer.
        if (SUCCEEDED(hr)) hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            100000, 0, &format, nullptr);
        if (SUCCEEDED(hr)) hr = client->GetBufferSize(&capacity);
        if (SUCCEEDED(hr)) hr = client->GetService(IID_PPV_ARGS(&render));
        { std::lock_guard lock(mutex); ok = SUCCEEDED(hr); ready = true; cv.notify_all(); }
        bool started = false;
        if (SUCCEEDED(hr)) {
            std::unique_lock lock(mutex);
            while (!quit) {
                if (!run) { cv.wait(lock, [&] { return quit || run; }); continue; }
                if (!started) {
                    lock.unlock(); hr = client->Start(); lock.lock();
                    if (FAILED(hr)) { ok = false; break; } started = true;
                }
                lock.unlock(); UINT32 padding = 0;
                hr = client->GetCurrentPadding(&padding);
                const UINT32 frames = SUCCEEDED(hr) && capacity > padding ? capacity - padding : 0;
                BYTE* output = nullptr;
                if (frames && SUCCEEDED(render->GetBuffer(frames, &output)) && output) {
                    lock.lock();
                    auto* dst = reinterpret_cast<int16_t*>(output);
                    const size_t elements = size_t(frames) * 2, available = std::min(elements, count);
                    for (size_t i = 0; i < available; ++i) dst[i] = pcm[(head + i) % kCapacity];
                    std::fill(dst + available, dst + elements, int16_t(0));
                    head = (head + available) % kCapacity; count -= available;
                    lock.unlock(); render->ReleaseBuffer(frames, 0);
                }
                lock.lock(); cv.wait_for(lock, std::chrono::milliseconds(2), [&] { return quit; });
            }
        }
        if (started) client->Stop();
        render.Reset(); client.Reset(); device.Reset(); enumerator.Reset();
        if (SUCCEEDED(com)) CoUninitialize();
    }
};
WasapiAudioRenderer::WasapiAudioRenderer() : impl_(std::make_unique<Impl>()) {}
WasapiAudioRenderer::~WasapiAudioRenderer() { Stop(); }
bool WasapiAudioRenderer::Initialize(const std::wstring& endpoint) {
    Stop();
    { std::lock_guard lock(impl_->mutex); impl_->quit = impl_->ready = impl_->ok = impl_->run = false; }
    impl_->worker = std::thread([this, endpoint] { impl_->process(endpoint); });
    std::unique_lock lock(impl_->mutex); impl_->cv.wait(lock, [&] { return impl_->ready; }); return impl_->ok;
}
void WasapiAudioRenderer::Start() { std::lock_guard lock(impl_->mutex); if (impl_->ok) impl_->run = true; impl_->cv.notify_all(); }
void WasapiAudioRenderer::Stop() {
    { std::lock_guard lock(impl_->mutex); impl_->quit = true; impl_->cv.notify_all(); }
    if (impl_->worker.joinable()) impl_->worker.join();
    std::lock_guard lock(impl_->mutex); impl_->ok = impl_->run = false; impl_->head = impl_->count = 0;
}
void WasapiAudioRenderer::Flush() { std::lock_guard lock(impl_->mutex); impl_->head = impl_->count = 0; }
void WasapiAudioRenderer::RenderPcm16(std::span<const int16_t> samples, int channels) {
    if ((channels != 1 && channels != 2) || samples.empty() || samples.size() % size_t(channels)) return;
    std::lock_guard lock(impl_->mutex); if (!impl_->ok || !impl_->run) return;
    size_t frames = samples.size() / size_t(channels);
    if (frames > Impl::kCapacity / 2) { samples = samples.last((Impl::kCapacity / 2) * size_t(channels)); frames = Impl::kCapacity / 2; }
    const size_t elements = frames * 2;
    if (elements > Impl::kCapacity - impl_->count) {
        const size_t drop = elements - (Impl::kCapacity - impl_->count);
        impl_->head = (impl_->head + drop) % Impl::kCapacity; impl_->count -= drop;
    }
    for (size_t i = 0; i < frames; ++i) {
        impl_->pcm[(impl_->head + impl_->count++) % Impl::kCapacity] = samples[i * size_t(channels)];
        impl_->pcm[(impl_->head + impl_->count++) % Impl::kCapacity] = samples[i * size_t(channels) + size_t(channels - 1)];
    }
}
} // namespace km::audio
