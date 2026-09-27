// Device-side audio output: the RTC audio callback only pushes into a bounded
// queue, a dedicated worker thread owns the AudioQueue and hands it to the
// selected output endpoint (stage 9 手順2).
//
// Everything that can touch the device runs on the worker: create, prime,
// start, device change and dispose. Two rules keep it correct:
//   - AudioQueueStop/Dispose run WITHOUT the state lock, because they wait for
//     the completion callback, and that callback takes the state lock;
//   - the completion callback ignores buffers from any queue that is not the
//     current one, so a disposed endpoint cannot feed its buffers back.
#include "audio_output.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace km::mac {
namespace {
constexpr uint32_t kBufferCount = 4;     // buffers in flight at the device
constexpr double kBufferSeconds = 0.020; // one 20 ms pull per completion
constexpr double kQueueSeconds = 0.400;  // hard cap on queued audio
constexpr double kHighWaterSeconds = 0.120;
constexpr double kTargetSeconds = 0.040;

std::string OSError(const char* what, OSStatus status) {
    char text[64];
    std::snprintf(text, sizeof(text), " (%ld)", long(status));
    return std::string(what) + text;
}

uint32_t DefaultOutputDeviceID() {
    AudioObjectPropertyAddress address = {kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioDeviceID id = kAudioObjectUnknown;
    UInt32 size = sizeof(id);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, &id) !=
        noErr)
        return 0;
    return id;
}

std::string DeviceName(AudioDeviceID id) {
    AudioObjectPropertyAddress address = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain};
    CFStringRef name = nullptr;
    UInt32 size = sizeof(name);
    if (AudioObjectGetPropertyData(id, &address, 0, nullptr, &size, &name) != noErr || !name)
        return std::string();
    char buffer[256];
    const CFIndex used = CFStringGetCString(name, buffer, sizeof(buffer), kCFStringEncodingUTF8);
    CFRelease(name);
    return used > 0 ? std::string(buffer) : std::string();
}

double DeviceRate(AudioDeviceID id) {
    AudioObjectPropertyAddress address = {kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    Float64 rate = 0;
    UInt32 size = sizeof(rate);
    if (AudioObjectGetPropertyData(id, &address, 0, nullptr, &size, &rate) != noErr)
        return 0.0;
    return double(rate);
}

// kAudioQueueProperty_CurrentDevice takes the device UID as a CFStringRef, not
// an AudioDeviceID; passing the numeric id fails with kAudioQueueErr_InvalidDevice.
CFStringRef DeviceUID(AudioDeviceID id) {
    AudioObjectPropertyAddress address = {kAudioDevicePropertyDeviceUID,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    CFStringRef uid = nullptr;
    UInt32 size = sizeof(uid);
    if (AudioObjectGetPropertyData(id, &address, 0, nullptr, &size, &uid) != noErr || !uid)
        return nullptr;
    return uid; // the getter hands back a +1 reference
}

bool IsOutputDevice(AudioDeviceID id) {
    AudioObjectPropertyAddress address = {kAudioDevicePropertyStreams,
        kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    return AudioObjectGetPropertyDataSize(id, &address, 0, nullptr, &size) == noErr && size > 0 &&
           (size % sizeof(AudioDeviceID)) == 0;
}
} // namespace

struct AudioOutput::Impl {
    Impl(uint32_t sampleRate, uint32_t channels)
        : rate_(sampleRate),
          channels_(channels),
          queue_(size_t(sampleRate * kQueueSeconds) * channels,
              size_t(sampleRate * kHighWaterSeconds) * channels,
              size_t(sampleRate * kTargetSeconds) * channels),
          bufferElements_(size_t(sampleRate * kBufferSeconds) * channels) {}

    ~Impl() { Stop(); }

    bool Start(std::string& error);
    void Stop();
    bool SetDevice(uint32_t device, std::string& error);
    void Push(const int16_t* data, size_t elements) { queue_.push(data, elements); }
    void SetMuted(bool muted);
    bool Running() const;
    bool Muted() const;
    Stats GetStats() const;

private:
    static constexpr int kCmdNone = 0;
    static constexpr int kCmdStart = 1;
    static constexpr int kCmdSetDevice = 2;

    static void QueueCallback(void* user, AudioQueueRef queue, AudioQueueBufferRef buffer) {
        static_cast<Impl*>(user)->OnBufferDone(queue, buffer);
    }
    void WorkerLoop();
    bool RunCommand(int kind, uint32_t device, std::string& error);
    void ExecuteCommand();  // worker thread, never holds m_ for the device calls
    bool CreateQueue(uint32_t device, std::string& error);
    static void DisposeQueue(AudioQueueRef queue); // no lock held
    void OnBufferDone(AudioQueueRef queue, AudioQueueBufferRef buffer);
    void FillBuffer(AudioQueueBufferRef buffer);

    const uint32_t rate_;
    const uint32_t channels_;
    AudioPcmQueue queue_;
    const size_t bufferElements_;

    mutable std::mutex m_;
    std::condition_variable cv_;
    std::vector<AudioQueueBufferRef> free_;
    std::thread worker_;
    bool stopRequested_ = false;
    bool workerAlive_ = false;
    bool running_ = false;
    bool muted_ = false;
    bool cmdPending_ = false;
    bool cmdDone_ = false;
    int cmdKind_ = kCmdNone;
    uint32_t cmdDevice_ = 0;
    bool cmdOk_ = false;
    std::string cmdError_;

    AudioQueueRef aq_ = nullptr; // m_: the callback compares against it
    AudioDeviceID deviceID_ = 0;
    double deviceRate_ = 0.0;
    std::string deviceName_;
    std::chrono::steady_clock::time_point startAt_{};

    std::atomic<uint64_t> buffersCompleted_{0};
    std::atomic<uint64_t> mutedBuffers_{0};
    std::atomic<uint64_t> enqueuedFrames_{0};
    std::atomic<uint64_t> underruns_{0};
    std::atomic<uint64_t> enqueueErrors_{0};
};

bool AudioOutput::Impl::Start(std::string& error) {
    {
        std::lock_guard<std::mutex> lock(m_);
        if (worker_.joinable()) {
            error = "audio output is already running";
            return false;
        }
        stopRequested_ = false;
        workerAlive_ = true; // the waiter below must not see a dead worker yet
    }
    worker_ = std::thread(&Impl::WorkerLoop, this);
    if (!RunCommand(kCmdStart, 0, error)) {
        Stop();
        return false;
    }
    return true;
}

void AudioOutput::Impl::Stop() {
    {
        std::lock_guard<std::mutex> lock(m_);
        stopRequested_ = true;
        if (cmdPending_) { // a waiter must not sit out its timeout
            cmdOk_ = false;
            cmdError_ = "audio output stopped";
            cmdPending_ = false;
            cmdDone_ = true;
        }
        cv_.notify_all();
    }
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(m_);
    free_.clear();
    running_ = false;
    startAt_ = {};
}

bool AudioOutput::Impl::SetDevice(uint32_t device, std::string& error) {
    return RunCommand(kCmdSetDevice, device, error);
}

void AudioOutput::Impl::SetMuted(bool muted) {
    std::lock_guard<std::mutex> lock(m_);
    muted_ = muted;
}

bool AudioOutput::Impl::Running() const {
    std::lock_guard<std::mutex> lock(m_);
    return running_;
}

bool AudioOutput::Impl::Muted() const {
    std::lock_guard<std::mutex> lock(m_);
    return muted_;
}

AudioOutput::Stats AudioOutput::Impl::GetStats() const {
    const auto q = queue_.stats();
    Stats out;
    out.pushedElements = q.pushedElements;
    out.poppedElements = q.poppedElements;
    out.droppedElements = q.droppedElements;
    out.depthHigh = q.depthHigh;
    out.pushMaxNs = q.pushMaxNs;
    out.depth = queue_.depth();
    out.underruns = underruns_.load(std::memory_order_relaxed);
    out.mutedBuffers = mutedBuffers_.load(std::memory_order_relaxed);
    out.buffersCompleted = buffersCompleted_.load(std::memory_order_relaxed);
    out.enqueuedFrames = enqueuedFrames_.load(std::memory_order_relaxed);
    out.enqueueErrors = enqueueErrors_.load(std::memory_order_relaxed);
    out.playSeconds = double(out.buffersCompleted) * kBufferSeconds;
    std::lock_guard<std::mutex> lock(m_);
    out.deviceID = deviceID_;
    out.deviceRate = deviceRate_;
    out.deviceName = deviceName_;
    out.running = running_;
    out.muted = muted_;
    if (running_ && startAt_ != std::chrono::steady_clock::time_point{})
        out.wallSeconds = std::chrono::duration<double>(
                              std::chrono::steady_clock::now() - startAt_).count();
    return out;
}

// One command at a time, executed on the worker and waited for by the caller:
// the caller never touches the device, and a worker that stops mid-command does
// not leave the caller blocked.
bool AudioOutput::Impl::RunCommand(int kind, uint32_t device, std::string& error) {
    std::unique_lock<std::mutex> lock(m_);
    if (cmdPending_) {
        error = "an audio device command is already in flight";
        return false;
    }
    cmdKind_ = kind;
    cmdDevice_ = device;
    cmdPending_ = true;
    cmdDone_ = false;
    cmdOk_ = false;
    cmdError_.clear();
    cv_.notify_all();
    const bool finished = cv_.wait_for(lock, std::chrono::seconds(10),
        [this] { return cmdDone_ || !workerAlive_; });
    if (!finished) {
        error = "audio device command timed out";
        return false;
    }
    if (!cmdOk_) {
        error = cmdError_.empty() ? "audio endpoint is no longer available" : cmdError_;
        return false;
    }
    return true;
}

// The worker is the single owner of every AudioQueue call: commands from the
// API, buffer refills from the completion callback, disposal at stop.
void AudioOutput::Impl::WorkerLoop() {
    std::unique_lock<std::mutex> lock(m_);
    while (true) {
        cv_.wait(lock, [this] { return stopRequested_ || cmdPending_ || !free_.empty(); });
        if (stopRequested_) break;
        if (cmdPending_) {
            // Device work runs with the lock released: AudioQueueStop and
            // AudioQueueDispose wait for the completion callback, which takes
            // this same lock.
            lock.unlock();
            ExecuteCommand();
            lock.lock();
            continue;
        }
        AudioQueueBufferRef buffer = free_.back();
        free_.pop_back();
        lock.unlock();
        FillBuffer(buffer); // queue pop + memset only; no device call
        lock.lock();
        if (stopRequested_) break;
        if (!aq_) continue; // the endpoint went away with the command
        const OSStatus status = AudioQueueEnqueueBuffer(aq_, buffer, 0, nullptr);
        if (status != noErr) {
            // The endpoint refused the buffer: count it and stop rather than
            // spin; disposal still happens on this thread, without the lock.
            enqueueErrors_.fetch_add(1, std::memory_order_relaxed);
            break;
        }
    }
    AudioQueueRef queue = aq_;
    aq_ = nullptr;
    free_.clear();
    running_ = false;
    cmdPending_ = false;
    cmdDone_ = true;
    cv_.notify_all();
    lock.unlock();
    DisposeQueue(queue);
    lock.lock();
    workerAlive_ = false;
    cv_.notify_all();
}

void AudioOutput::Impl::ExecuteCommand() {
    int kind = kCmdNone;
    uint32_t device = 0;
    {
        std::lock_guard<std::mutex> lock(m_);
        kind = cmdKind_;
        device = cmdDevice_;
    }
    std::string error;
    bool ok = false;
    switch (kind) {
        case kCmdStart:
        case kCmdSetDevice: ok = CreateQueue(device, error); break;
        default: error = "unknown audio device command"; break;
    }
    std::lock_guard<std::mutex> lock(m_);
    cmdOk_ = ok;
    cmdError_ = error;
    cmdPending_ = false;
    cmdDone_ = true;
    cv_.notify_all();
}

// Builds the new queue completely before touching the running one: a device
// that refuses to open leaves the current endpoint playing.
bool AudioOutput::Impl::CreateQueue(uint32_t device, std::string& error) {
    AudioDeviceID target = 0;
    {
        std::lock_guard<std::mutex> lock(m_);
        target = device ? device : (deviceID_ ? deviceID_ : DefaultOutputDeviceID());
    }
    if (!target) {
        error = "no output device";
        return false;
    }
    AudioStreamBasicDescription format{};
    format.mSampleRate = double(rate_);
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = sizeof(int16_t) * channels_;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = sizeof(int16_t) * channels_;
    format.mChannelsPerFrame = channels_;
    format.mBitsPerChannel = 16;

    AudioQueueRef queue = nullptr;
    OSStatus status = AudioQueueNewOutput(&format, QueueCallback, this, nullptr, nullptr, 0, &queue);
    if (status != noErr) {
        error = OSError("AudioQueueNewOutput", status);
        return false;
    }
    if (target != DefaultOutputDeviceID()) {
        CFStringRef uid = DeviceUID(target);
        if (!uid) {
            error = "output device has no UID";
            AudioQueueDispose(queue, true);
            return false;
        }
        status = AudioQueueSetProperty(queue, kAudioQueueProperty_CurrentDevice, &uid,
            sizeof(uid));
        CFRelease(uid);
        if (status != noErr) {
            error = OSError("AudioQueueSetProperty(CurrentDevice)", status);
            AudioQueueDispose(queue, true);
            return false;
        }
    }
    const UInt32 bytes = UInt32(bufferElements_ * sizeof(int16_t));
    std::vector<AudioQueueBufferRef> buffers;
    buffers.reserve(kBufferCount);
    for (uint32_t i = 0; i < kBufferCount; ++i) {
        AudioQueueBufferRef buffer = nullptr;
        status = AudioQueueAllocateBuffer(queue, bytes, &buffer);
        if (status != noErr) {
            error = OSError("AudioQueueAllocateBuffer", status);
            AudioQueueDispose(queue, true);
            return false;
        }
        // Prime with silence: the endpoint starts before the RTC stream exists,
        // and those four buffers are startup, not an underrun.
        std::memset(buffer->mAudioData, 0, bytes);
        buffer->mAudioDataByteSize = bytes;
        buffers.push_back(buffer);
    }
    for (AudioQueueBufferRef buffer : buffers) {
        status = AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
        if (status != noErr) {
            error = OSError("AudioQueueEnqueueBuffer", status);
            AudioQueueDispose(queue, true);
            return false;
        }
    }

    // Swap before Start: the callback only accepts buffers of the current queue.
    AudioQueueRef previous = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_);
        previous = aq_;
        aq_ = queue;
        free_.clear(); // buffers of the previous endpoint are disposed with it
    }
    DisposeQueue(previous);

    status = AudioQueueStart(queue, nullptr);
    if (status != noErr) {
        error = OSError("AudioQueueStart", status);
        {
            std::lock_guard<std::mutex> lock(m_);
            aq_ = nullptr;
        }
        AudioQueueDispose(queue, true);
        return false;
    }

    // Start from the live depth: the reservoir keeps the endpoint from pulling
    // an empty queue while the RTC stream is still connecting, and the high
    // water policy returns here whenever the endpoint stalls.
    queue_.clear();
    const size_t reservoir = size_t(rate_ * kTargetSeconds) * channels_;
    const std::vector<int16_t> silence(reservoir, 0);
    queue_.push(silence.data(), silence.size());

    buffersCompleted_ = 0;
    mutedBuffers_ = 0;
    enqueuedFrames_ = 0;
    underruns_ = 0;
    enqueueErrors_ = 0;
    std::lock_guard<std::mutex> lock(m_);
    deviceID_ = target;
    deviceRate_ = DeviceRate(target);
    deviceName_ = DeviceName(target);
    startAt_ = std::chrono::steady_clock::now();
    running_ = true;
    return true;
}

// No lock held: AudioQueueStop/Dispose may wait for the completion callback.
void AudioOutput::Impl::DisposeQueue(AudioQueueRef queue) {
    if (!queue) return;
    AudioQueueStop(queue, true);
    AudioQueueDispose(queue, true);
}

void AudioOutput::Impl::OnBufferDone(AudioQueueRef queue, AudioQueueBufferRef buffer) {
    std::lock_guard<std::mutex> lock(m_);
    if (queue != aq_) return; // buffer of a queue that has already been replaced
    free_.push_back(buffer);
    buffersCompleted_.fetch_add(1, std::memory_order_relaxed);
    cv_.notify_one();
}

void AudioOutput::Impl::FillBuffer(AudioQueueBufferRef buffer) {
    int16_t* dst = static_cast<int16_t*>(buffer->mAudioData);
    const size_t got = queue_.pop(dst, bufferElements_);
    if (got < bufferElements_) {
        std::fill(dst + got, dst + bufferElements_, int16_t(0));
        underruns_.fetch_add(1, std::memory_order_relaxed);
    }
    bool muted = false;
    {
        std::lock_guard<std::mutex> lock(m_);
        muted = muted_;
    }
    if (muted) {
        // Mute silences the endpoint but keeps draining, so unmute resumes at
        // the live depth instead of playing the muted backlog.
        std::fill(dst, dst + bufferElements_, int16_t(0));
        mutedBuffers_.fetch_add(1, std::memory_order_relaxed);
    }
    buffer->mAudioDataByteSize = UInt32(bufferElements_ * sizeof(int16_t));
    enqueuedFrames_.fetch_add(bufferElements_ / channels_, std::memory_order_relaxed);
}

AudioOutput::AudioOutput(uint32_t sampleRate, uint32_t channels)
    : impl_(std::make_unique<Impl>(sampleRate, channels)) {}

AudioOutput::~AudioOutput() = default;

std::vector<AudioOutput::DeviceInfo> AudioOutput::ListOutputDevices() {
    std::vector<DeviceInfo> devices;
    AudioObjectPropertyAddress address = {kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) !=
            noErr ||
        size == 0 || size > 4096 * sizeof(AudioDeviceID))
        return devices;
    std::vector<AudioDeviceID> ids(size / sizeof(AudioDeviceID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size,
            ids.data()) != noErr)
        return devices;
    const AudioDeviceID fallback = DefaultOutputDeviceID();
    for (AudioDeviceID id : ids) {
        if (!IsOutputDevice(id)) continue;
        DeviceInfo info;
        info.id = id;
        info.name = DeviceName(id);
        info.nominalRate = DeviceRate(id);
        info.isDefault = id == fallback;
        devices.push_back(info);
    }
    return devices;
}

uint32_t AudioOutput::DefaultOutputDevice() { return DefaultOutputDeviceID(); }

bool AudioOutput::start(std::string& error) { return impl_->Start(error); }
void AudioOutput::stop() { impl_->Stop(); }
void AudioOutput::push(const int16_t* pcm, size_t elements) {
    if (pcm && elements) impl_->Push(pcm, elements);
}
void AudioOutput::setMuted(bool muted) { impl_->SetMuted(muted); }
bool AudioOutput::setDevice(uint32_t deviceID, std::string& error) {
    return impl_->SetDevice(deviceID, error);
}
bool AudioOutput::running() const { return impl_->Running(); }
bool AudioOutput::muted() const { return impl_->Muted(); }
AudioOutput::Stats AudioOutput::stats() const { return impl_->GetStats(); }

} // namespace km::mac
