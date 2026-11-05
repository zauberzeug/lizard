#pragma once

#include "variable.h"
#include <memory>
#include <stdexcept>

// An integer property that counts the transitions of a state machine, so a host that misses frames still sees them.
// The value modulo `states` is the current state, 0 is idle, and it wraps at `period`, the largest multiple of
// `states` up to 255, so a host compares two values modulo `period`.
// Every accepted command takes the counter exactly `states` steps from the idle before it to its own end, also when
// it cuts a running cycle short. A refused command leaves it unchanged, so a host that sends several commands
// without waiting cannot tell which one was refused once a later one is accepted; it waits until each command
// has moved the counter (or a timeout) before it sends the next.
template <typename State>
class StateCounter : public IntegerVariable {
public:
    static constexpr int64_t MAX_VALUE = 255;

    const int64_t states;
    const int64_t period;

    StateCounter(const int64_t states)
        : IntegerVariable(0), states(checked_states(states)), period(MAX_VALUE / this->states * this->states) {
    }

    State state() const {
        return static_cast<State>(this->integer_slot % this->states);
    }

    // Counts forward to `state`; staying in `state` counts nothing.
    void set(const State state) {
        const int64_t index = static_cast<int64_t>(state);
        if (index < 0 || index >= this->states) {
            throw std::runtime_error("state out of range");
        }
        const int64_t steps = (index - static_cast<int64_t>(this->state()) + this->states) % this->states;
        this->integer_slot = (this->integer_slot + steps) % this->period;
    }

private:
    // checked in the initializer of `states`, so `period` is never computed from an invalid count
    static int64_t checked_states(const int64_t states) {
        if (states < 2 || states > MAX_VALUE) {
            throw std::runtime_error("a state counter needs between 2 and 255 states");
        }
        return states;
    }
};
