#pragma once
// Leases are thread-affine; destroy each lease on the thread which entered it.
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
namespace km {
// A callback takes a lease BEFORE dereferencing its captured owner pointer.
// Close is a control-thread operation, not callable from inside a leased callback.
class CallbackGate : public std::enable_shared_from_this<CallbackGate> {
public:
    class Lease {
    public:
        Lease() = default;
        explicit Lease(std::shared_ptr<CallbackGate> gate) : gate_(std::move(gate)) {}
        Lease(Lease&&) = default;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        ~Lease() {
            if (!gate_) return;
            std::lock_guard lock(gate_->mutex_);
            auto it = gate_->users_.find(std::this_thread::get_id());
            if (--it->second == 0) gate_->users_.erase(it);
            gate_->cv_.notify_all();
        }
        explicit operator bool() const { return bool(gate_); }
    private: std::shared_ptr<CallbackGate> gate_;
    };
    Lease enter() {
        std::lock_guard lock(mutex_);
        if (!open_) return {};
        ++users_[std::this_thread::get_id()]; return Lease(shared_from_this());
    }
    void closeAndWait() {
        std::unique_lock lock(mutex_);
        if (users_.contains(std::this_thread::get_id())) throw std::logic_error("Close must run on the control thread");
        open_ = false; cv_.wait(lock, [&] { return users_.empty(); });
    }
private:
    std::mutex mutex_; std::condition_variable cv_;
    bool open_ = true; std::map<std::thread::id, size_t> users_;
};
} // namespace km
