#pragma once
// Device-side audio output for the host (stage 9 手順2).
//
//   RTC audio callback -> bounded queue (push only, never waits for a device)
//   dedicated worker   -> AudioQueue -> the selected output endpoint
//
// The worker owns every AudioQueue call that can take time: creation, priming,
// start, device change and disposal. The RTC callback only copies into the
// bounded queue, so an endpoint that stalls cannot hold the RTC thread.
#include "audio_pcm_queue.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace km::mac {

class AudioOutput {
public:
    struct DeviceInfo {
        uint32_t id = 0;
        std::string name;
        double nominalRate = 0.0;
        bool isDefault = false;
    };

    struct Stats {
        // queue side
        uint64_t pushedElements = 0;
        uint64_t poppedElements = 0;
        uint64_t droppedElements = 0;
        size_t depth = 0;
        size_t depthHigh = 0;
        uint64_t pushMaxNs = 0;      // longest RTC push (contention evidence)
        // device side
        uint64_t underruns = 0;      // device buffer filled short of its size
        uint64_t mutedBuffers = 0;   // buffers written as silence while muted
        uint64_t buffersCompleted = 0; // buffers the device finished playing
        uint64_t enqueuedFrames = 0;
        uint64_t enqueueErrors = 0;    // AudioQueueEnqueueBuffer failures
        uint32_t deviceID = 0;
        double deviceRate = 0.0;
        std::string deviceName;
        // playback vs wall clock, from the device callbacks since start
        double playSeconds = 0.0;
        double wallSeconds = 0.0;
        bool running = false;
        bool muted = false;
    };

    // Interleaved int16; 48 kHz stereo matches the Opus decoder's PCM.
    explicit AudioOutput(uint32_t sampleRate = 48000, uint32_t channels = 2);
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    static std::vector<DeviceInfo> ListOutputDevices();
    static uint32_t DefaultOutputDevice();

    bool start(std::string& error);
    void stop();
    void push(const int16_t* pcm, size_t elements);
    void setMuted(bool muted);
    bool setDevice(uint32_t deviceID, std::string& error);
    bool running() const;
    bool muted() const;
    Stats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace km::mac
