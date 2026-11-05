#pragma once

#include "variable.h"
#include <stdexcept>

// Number of bits that can hold `count` distinct values.
constexpr int64_t bits_for(const int64_t count) {
    return count <= 1 ? 0 : 1 + bits_for((count + 1) / 2);
}

// An integer property that counts the transitions of a state machine, so a host that misses frames still sees them.
// The low `STATE_BITS` of the value are the current state, the bits above are the epoch, the number of completed
// cycles. A module reports an accepted command as `set` of its idle state followed by `set` of the command's state,
// so every command takes exactly one epoch; the host protocol is documented under "State Counters" in the module
// reference.
// `State` is an enum with a sentinel enumerator `COUNT` one past the last state.
template <typename State>
class StateCounter : public IntegerVariable {
public:
    static constexpr int64_t VALUE_BITS = 8;
    static constexpr int64_t STATES = static_cast<int64_t>(State::COUNT);
    static constexpr int64_t STATE_BITS = bits_for(STATES);
    static constexpr int64_t STATE_MASK = (1 << STATE_BITS) - 1;
    static constexpr int64_t PERIOD = 1 << VALUE_BITS;
    static_assert(STATES >= 2 && 2 * (STATE_MASK + 1) < PERIOD, "a full cycle has to fit in half a period");

    StateCounter() : IntegerVariable(0) {
    }

    State state() const {
        return static_cast<State>(this->integer_slot & STATE_MASK);
    }

    int64_t epoch() const {
        return this->integer_slot >> STATE_BITS;
    }

    // Whether counter value `a` was reached after `b`, for values less than half a period apart.
    static bool is_after(const int64_t a, const int64_t b) {
        const int64_t distance = (a - b) & (PERIOD - 1);
        return distance > 0 && 2 * distance < PERIOD;
    }

    // Counts forward to `state`: a state not after the current one starts the next epoch; staying counts nothing.
    void set(const State state) {
        const int64_t index = static_cast<int64_t>(state);
        if (index < 0 || index >= STATES) {
            throw std::runtime_error("state out of range");
        }
        const int64_t current = static_cast<int64_t>(this->state());
        if (index == current) {
            return;
        }
        const int64_t epoch = this->epoch() + (index < current ? 1 : 0);
        this->integer_slot = ((epoch << STATE_BITS) | index) & (PERIOD - 1);
    }
};
