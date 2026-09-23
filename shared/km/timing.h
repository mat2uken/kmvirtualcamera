#pragma once
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

namespace km {
// Single serial owner. Reset on a new SSRC, connection or timestamp discontinuity.
// Consecutive timestamps must be less than 2^31 ticks apart; reordering is allowed.
class TimestampUnwrapper32 {
public:
    int64_t unwrap(uint32_t value) {
        if (!last_) { last_ = value; unwrapped_ = value; return unwrapped_; }
        const uint32_t raw = value - *last_;
        const int64_t delta = raw <= 0x7fffffffu ? int64_t(raw) : int64_t(raw) - (int64_t(1)<<32);
        unwrapped_ += delta; last_ = value; return unwrapped_;
    }
    void reset() { last_.reset(); unwrapped_ = 0; }
private:
    std::optional<uint32_t> last_;
    int64_t unwrapped_ = 0;
};
inline int64_t TicksToMicroseconds(int64_t ticks, uint32_t rate) {
    if (!rate) throw std::invalid_argument("zero clock rate");
    const int64_t whole = ticks / rate, remainder = ticks % rate;
    if (whole > std::numeric_limits<int64_t>::max()/1000000 ||
        whole < std::numeric_limits<int64_t>::min()/1000000)
        throw std::overflow_error("timestamp out of range");
    const int64_t a = whole * 1000000, b = remainder * 1000000 / rate;
    if ((b > 0 && a > std::numeric_limits<int64_t>::max()-b) ||
        (b < 0 && a < std::numeric_limits<int64_t>::min()-b))
        throw std::overflow_error("timestamp out of range");
    return a+b;
}
// Exact rational deadlines, not repeated 33ms or 16,666us increments.
// next() returns epoch + floor(index * 1e9 * denominator / numerator).
class RationalPacer {
public:
    RationalPacer(uint32_t numerator, uint32_t denominator, uint64_t epochNs)
        : numerator_(numerator), deadline_(epochNs) {
        if (!numerator || !denominator || numerator > 1000000 || denominator > 1000000)
            throw std::invalid_argument("invalid frame rate");
        const uint64_t period = 1000000000ull * denominator;
        whole_ = period/numerator; fraction_ = period%numerator;
    }
    uint64_t next() {
        const uint64_t result = deadline_;
        const uint64_t sum = remainder_ + fraction_;
        const uint64_t step = whole_ + sum/numerator_;
        if (deadline_ > std::numeric_limits<uint64_t>::max()-step)
            throw std::overflow_error("deadline out of range");
        deadline_ += step; remainder_ = sum%numerator_;
        return result;
    }
private:
    uint32_t numerator_;
    uint64_t deadline_, whole_=0, fraction_=0, remainder_=0;
};
} // namespace km
