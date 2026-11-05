#include "state_counter.h"
#include <stdexcept>

static constexpr int64_t MAX_VALUE = 255;

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
    return this->integer_value() % this->states;
}

void StateCounter::enter(const int64_t state) {
    if (state < 0 || state >= this->states) {
        throw std::runtime_error("state out of range");
    }
    this->advance((state - this->state() + this->states) % this->states);
}

void StateCounter::restart(const int64_t state) {
    if (state < 1 || state >= this->states) {
        throw std::runtime_error("state out of range");
    }
    this->advance((this->states - this->state()) % this->states + state);
}

void StateCounter::advance(const int64_t steps) {
    this->set_integer_value((this->integer_value() + steps) % this->period);
}
