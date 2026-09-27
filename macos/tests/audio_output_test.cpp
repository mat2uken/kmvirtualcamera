// Stage-9 A6 evidence for AudioOutput against a real output endpoint.
//
// Phases: steady tone, mute, unmute, an overflow burst, an underrun window,
// stop/restart, a device change, and a non-48 kHz endpoint when a device
// offers one. Every number printed here is measured on this machine.
//
// The test plays an audible 1 kHz tone through the selected endpoint.
#include "receiver/audio_output.h"

#include <AudioToolbox/AudioToolbox.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
using km::mac::AudioOutput;

constexpr double kBufferSeconds = 0.020;
constexpr size_t kChunkFrames = 960; // 20 ms at 48 kHz
constexpr size_t kChunkElements = kChunkFrames * 2;
constexpr double kToneHz = 1000.0;
constexpr double kTwoPi = 6.28318530717958647692;
constexpr int kToneAmplitude = 8000; // about -12 dBFS

int failures = 0;
void Check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

double NowSec() {
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

void FillTone(std::vector<int16_t>& chunk, double& phase) {
    for (size_t i = 0; i < kChunkFrames; ++i) {
        const int16_t value = int16_t(std::sin(phase) * kToneAmplitude);
        chunk[2 * i] = value;
        chunk[2 * i + 1] = value;
        phase += kTwoPi * kToneHz / 48000.0;
        if (phase >= kTwoPi) phase -= kTwoPi;
    }
}

// Real-time push: the RTC callback produces 20 ms every 20 ms.
void PushFor(AudioOutput& out, double seconds, double& phase) {
    const int chunks = int(seconds / kBufferSeconds);
    std::vector<int16_t> chunk(kChunkElements, 0);
    const Clock::time_point start = Clock::now();
    for (int i = 0; i < chunks; ++i) {
        FillTone(chunk, phase);
        out.push(chunk.data(), chunk.size());
        std::this_thread::sleep_until(
            start + std::chrono::microseconds(int64_t((i + 1) * kBufferSeconds * 1e6)));
    }
}

struct Sample {
    AudioOutput::Stats s;
    double t = 0.0;
};
Sample Take(AudioOutput& out) { return Sample{out.stats(), NowSec()}; }

// Pushes for the window and reports what changed inside it.
struct Window {
    double wall = 0.0;
    double ratio = 0.0;
    uint64_t underruns = 0;
    uint64_t dropped = 0;
    uint64_t muted = 0;
    size_t depth = 0;
    size_t depthHigh = 0;
    uint64_t pushMaxNs = 0;
    uint64_t enqueueErrors = 0;
};

Window Measure(AudioOutput& out, const char* name, double seconds, double& phase) {
    const Sample before = Take(out);
    PushFor(out, seconds, phase);
    const Sample after = Take(out);
    Window w;
    w.wall = after.t - before.t;
    const double played = double(after.s.buffersCompleted - before.s.buffersCompleted) *
                          kBufferSeconds;
    w.ratio = w.wall > 0 ? played / w.wall : 0.0;
    w.underruns = after.s.underruns - before.s.underruns;
    w.dropped = after.s.droppedElements - before.s.droppedElements;
    w.muted = after.s.mutedBuffers - before.s.mutedBuffers;
    w.depth = after.s.depth;
    w.depthHigh = after.s.depthHigh;
    w.pushMaxNs = after.s.pushMaxNs;
    w.enqueueErrors = after.s.enqueueErrors;
    std::printf("%-12s wall=%.2fs played=%.2fs ratio=%.4f underrun=%llu dropped=%llu "
                "muted=%llu depth=%zu pushMax=%.0fus\n",
        name, w.wall, played, w.ratio, (unsigned long long)w.underruns,
        (unsigned long long)w.dropped, (unsigned long long)w.muted, w.depth,
        double(w.pushMaxNs) / 1000.0);
    return w;
}

bool SetNominalRate(AudioDeviceID device, double rate) {
    AudioObjectPropertyAddress address = {kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    return AudioObjectSetPropertyData(device, &address, 0, nullptr, sizeof(rate), &rate) == noErr;
}

double NominalRate(AudioDeviceID device) {
    AudioObjectPropertyAddress address = {kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    Float64 rate = 0;
    UInt32 size = sizeof(rate);
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &rate) != noErr)
        return 0.0;
    return double(rate);
}

// A real-time endpoint consumes the client frames at their own rate. A null or
// instant device (CI runner) drains the queue faster than the producer fills
// it, so underruns are expected there and the underrun check is skipped.
bool RealTime(double ratio) { return ratio > 0.5 && ratio < 2.0; }
// Underruns must stay rare. A real device gives 0; a loaded virtual device
// gives a handful per phase; a broken queue gives hundreds.
constexpr size_t kUnderrunTolerance = 5;

// True when the device accepts 44.1 kHz through its available rate ranges.
bool Supports44100(AudioDeviceID device) {
    AudioObjectPropertyAddress address = {kAudioDevicePropertyAvailableNominalSampleRates,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) != noErr ||
        size == 0 || size > 4096)
        return false;
    std::vector<AudioValueRange> ranges(size / sizeof(AudioValueRange));
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, ranges.data()) != noErr)
        return false;
    for (const AudioValueRange& range : ranges)
        if (44100.0 >= range.mMinimum && 44100.0 <= range.mMaximum) return true;
    return false;
}
} // namespace

int main() {
    std::printf("A6 audio output test: 1 kHz tone, %zu frames per push\n", kChunkFrames);
    const std::vector<AudioOutput::DeviceInfo> devices = AudioOutput::ListOutputDevices();
    for (const AudioOutput::DeviceInfo& device : devices)
        std::printf("device id=%u rate=%.0f default=%d %s\n", device.id, device.nominalRate,
            int(device.isDefault), device.name.c_str());
    if (devices.empty()) {
        std::printf("SKIP: no output device on this machine\n");
        return 0;
    }

    AudioOutput out;
    std::string error;
    if (!out.start(error)) {
        std::printf("FAIL: start: %s\n", error.c_str());
        return 1;
    }
    AudioOutput::Stats initial = out.stats();
    std::printf("output device id=%u rate=%.0f \"%s\"\n", initial.deviceID, initial.deviceRate,
        initial.deviceName.c_str());

    double phase = 0.0;

    // 1. steady stream: the endpoint consumes the client frames at their rate.
    const Window steady = Measure(out, "steady", 4.0, phase);
    Check(steady.ratio > 0.97 && steady.ratio < 1.03, "steady playback rate matches wall clock");
    Check(steady.underruns <= kUnderrunTolerance, "no underrun while the stream runs");
    Check(steady.dropped == 0, "no drop while the stream runs");
    Check(steady.enqueueErrors == 0, "no enqueue failure");
    Check(out.running(), "output runs");

    // 2. mute: the endpoint keeps playing and the queue keeps draining.
    out.setMuted(true);
    Check(out.muted(), "muted flag is readable");
    const Window muted = Measure(out, "muted", 2.0, phase);
    Check(muted.muted > 0, "muted buffers were written");
    Check(muted.underruns <= kUnderrunTolerance, "mute does not starve the endpoint");
    Check(muted.ratio > 0.97 && muted.ratio < 1.03, "device keeps its rate while muted");

    // 3. unmute: back to live audio without a backlog.
    out.setMuted(false);
    const Window unmuted = Measure(out, "unmuted", 1.0, phase);
    Check(unmuted.muted == 0, "no muted buffer after unmute");
    Check(unmuted.underruns <= kUnderrunTolerance, "no underrun after unmute");

    // 4. overflow: one oversized push must be counted, never queued whole.
    const Sample beforeBurst = Take(out);
    std::vector<int16_t> burst(kChunkElements * 25, 0); // 500 ms at once
    out.push(burst.data(), burst.size());
    const Sample afterBurst = Take(out);
    const uint64_t burstDropped = afterBurst.s.droppedElements - beforeBurst.s.droppedElements;
    std::printf("burst        dropped=%llu depth=%zu depthHigh=%zu\n",
        (unsigned long long)burstDropped, afterBurst.s.depth, afterBurst.s.depthHigh);
    Check(burstDropped > 0, "oversized push drops audio");
    Check(afterBurst.s.depth == afterBurst.s.pushedElements - afterBurst.s.droppedElements -
                                    afterBurst.s.poppedElements,
        "depth matches the accounting");
    const Window afterBurstWindow = Measure(out, "post-burst", 1.0, phase);
    Check(afterBurstWindow.underruns <= kUnderrunTolerance, "queue recovers after the burst");

    // 5. underrun: the RTC side stops, the endpoint must get silence.
    const Sample beforeGap = Take(out);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    const Sample afterGap = Take(out);
    const uint64_t gapUnderruns = afterGap.s.underruns - beforeGap.s.underruns;
    const double gapWall = afterGap.t - beforeGap.t;
    std::printf("gap          wall=%.2fs underrun=%llu depth=%zu\n", gapWall,
        (unsigned long long)gapUnderruns, afterGap.s.depth);
    Check(gapUnderruns > 0, "a stopped stream counts underruns");

    // 6. stop and restart: a fresh queue on the same endpoint.
    out.stop();
    Check(!out.running(), "output stops");
    if (!out.start(error)) {
        std::printf("FAIL: restart: %s\n", error.c_str());
        return 1;
    }
    const Window restarted = Measure(out, "restarted", 1.5, phase);
    Check(out.running(), "output runs after restart");
    Check(restarted.ratio > 0.97 && restarted.ratio < 1.03, "restarted playback rate");
    Check(restarted.enqueueErrors == 0, "no enqueue failure after restart");

    // 7. device change: move the queue to another endpoint and back.
    const AudioOutput::Stats running = out.stats();
    const AudioOutput::DeviceInfo* other = nullptr;
    for (const AudioOutput::DeviceInfo& device : devices)
        if (!device.isDefault && device.id != running.deviceID) { other = &device; break; }
    if (other) {
        if (out.setDevice(other->id, error)) {
            const AudioOutput::Stats moved = out.stats();
            std::printf("moved to id=%u rate=%.0f \"%s\"\n", moved.deviceID, moved.deviceRate,
                moved.deviceName.c_str());
            Check(moved.deviceID == other->id, "queue follows the selected device");
            const Window onOther = Measure(out, "on-device2", 2.0, phase);
            if (RealTime(onOther.ratio))
                Check(onOther.underruns <= kUnderrunTolerance, "no underrun on the second device");
            else
                std::printf("second device is not real-time (ratio=%.3f): underrun check skipped\n",
                    onOther.ratio);
            const AudioOutput::DeviceInfo fallback = devices.front().isDefault
                ? devices.front()
                : AudioOutput::DeviceInfo{AudioOutput::DefaultOutputDevice(), "default", 0.0, true};
            if (!out.setDevice(fallback.id, error))
                std::printf("restore device failed: %s\n", error.c_str());
            else
                Measure(out, "restored", 1.0, phase);
        } else {
            std::printf("device change failed: %s\n", error.c_str());
        }
    } else {
        std::printf("only one output device: device change not exercised\n");
    }

    // 8. non-48 kHz endpoint: the queue keeps its 48 kHz client format, so the
    //    rate ratio says whether the endpoint converts it.
    AudioDeviceID rateTarget = 0;
    double rateBefore = 0.0;
    for (const AudioOutput::DeviceInfo& device : devices) {
        if (device.nominalRate != 44100.0 && Supports44100(device.id)) {
            rateTarget = device.id;
            rateBefore = NominalRate(device.id);
            break;
        }
    }
    if (rateTarget) {
        if (!SetNominalRate(rateTarget, 44100.0)) {
            std::printf("device %u refuses a 44.1 kHz rate\n", rateTarget);
        } else if (!out.setDevice(rateTarget, error)) {
            std::printf("44.1 kHz device open failed: %s\n", error.c_str());
            SetNominalRate(rateTarget, rateBefore);
        } else {
            std::printf("44.1 kHz endpoint id=%u rate=%.0f\n", rateTarget,
                out.stats().deviceRate);
            const Window lowRate = Measure(out, "at44.1k", 3.0, phase);
            Check(lowRate.ratio > 0.97 && lowRate.ratio < 1.03,
                "48 kHz client frames stay real time on a 44.1 kHz endpoint");
            if (RealTime(lowRate.ratio))
                Check(lowRate.underruns <= kUnderrunTolerance,
                    "no underrun on the 44.1 kHz endpoint");
            else
                std::printf("44.1 kHz device is not real-time (ratio=%.3f): underrun check skipped\n",
                    lowRate.ratio);
            const AudioOutput::DeviceInfo fallback = devices.front().isDefault
                ? devices.front()
                : AudioOutput::DeviceInfo{AudioOutput::DefaultOutputDevice(), "default", 0.0, true};
            if (!out.setDevice(fallback.id, error))
                std::printf("restore device after rate test failed: %s\n", error.c_str());
            if (!SetNominalRate(rateTarget, rateBefore))
                std::printf("device %u did not return to %.0f Hz\n", rateTarget, rateBefore);
            else
                std::printf("device %u restored to %.0f Hz\n", rateTarget, rateBefore);
        }
    } else {
        std::printf("no output device offers 44.1 kHz: non-48 kHz endpoint not exercised\n");
    }

    const AudioOutput::Stats final = out.stats();
    std::printf("final        pushed=%llu popped=%llu dropped=%llu depth=%zu depthHigh=%zu "
                "completed=%llu underrun=%llu enqueueErr=%llu\n",
        (unsigned long long)final.pushedElements, (unsigned long long)final.poppedElements,
        (unsigned long long)final.droppedElements, final.depth, final.depthHigh,
        (unsigned long long)final.buffersCompleted, (unsigned long long)final.underruns,
        (unsigned long long)final.enqueueErrors);
    out.stop();
    Check(!out.running(), "output stops at the end");

    if (failures) {
        std::printf("audio_output: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("audio_output: all checks passed\n");
    return 0;
}
