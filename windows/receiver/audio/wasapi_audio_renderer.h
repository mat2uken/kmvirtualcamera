#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string>
namespace km::audio {
class WasapiAudioRenderer {
public:
    WasapiAudioRenderer();
    ~WasapiAudioRenderer();
    bool Initialize(const std::wstring& endpointId);
    void Start();
    void Stop();
    void Flush();
    void RenderPcm16(std::span<const int16_t> interleavedSamples, int channels);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace km::audio
