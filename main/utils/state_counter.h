#pragma once

#include "variable.h"
#include <stdexcept>

// An integer property that counts the transitions of a state machine, so a host that misses frames still sees them.
// The value modulo `states` is the current state, 0 is idle, and it wraps at `period`, the largest multiple of
// `states` up to 255, so a host compares two values modulo `period`.
// Every accepted command takes the counter exactly `states` steps from the idle before it to its own end, also when
// it cuts a running cycle short. A refused command leaves it unchanged, so a host that sends several commands
// without waiting cannot tell which one was refused once a later one is accepted; it waits until each command
// has moved the counter (or a timeout) before it sends the next.
// `State` is an enum with a sentinel enumerator `COUNT` one past the last state.
template <typename State>
class StateCounter : public IntegerVariable {
public:
    static constexpr int64_t MAX_VALUE = 255;
    static constexpr int64_t states = static_cast<int64_t>(State::COUNT);
    static_assert(states >= 2 && states <= MAX_VALUE, "a state counter needs between 2 and 255 states");
    static constexpr int64_t period = MAX_VALUE / states * states;

    StateCounter() : IntegerVariable(0) {
    }

    State state() const {
        return static_cast<State>(this->integer_slot % states);
    }

    // Counts forward to `state`; staying in `state` counts nothing.
    void set(const State state) {
        const int64_t index = static_cast<int64_t>(state);
        if (index < 0 || index >= states) {
            throw std::runtime_error("state out of range");
        }
        const int64_t steps = (index - static_cast<int64_t>(this->state()) + states) % states;
        this->integer_slot = (this->integer_slot + steps) % period;
    }
};
