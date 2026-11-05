#include "state_counter.h"
#include <stdexcept>

static constexpr int64_t MAX_VALUE = 255;
static constexpr int64_t IDLE = 0;

// checked in the initializer of `states`, so `period` is never computed from an invalid count
static int64_t checked_states(const int64_t states) {
    if (states < 2 || states > MAX_VALUE) {
        throw std::runtime_error("a state counter needs between 2 and 255 states");
    }
    return states;
}

StateCounter::StateCounter(const int64_t states)
    : IntegerVariable(0), states(checked_states(states)), period(MAX_VALUE / this->states * this->states) {
}

int64_t StateCounter::state() const {
    return this->integer_slot % this->states;
}

void StateCounter::set(const int64_t state) {
    if (state < 0 || state >= this->states) {
        throw std::runtime_error("state out of range");
    }
    const int64_t steps = (state - this->state() + this->states) % this->states;
    this->integer_slot = (this->integer_slot + steps) % this->period;
}

void StateCounter::restart(const int64_t state) {
    if (state == IDLE) {
        throw std::runtime_error("a cycle cannot start in idle");
    }
    this->set(IDLE);
    this->set(state);
}
