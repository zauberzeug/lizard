#include "state_counter.h"
#include <stdexcept>

static constexpr int64_t MAX_VALUE = 255;

StateCounter::StateCounter(const int64_t states)
    : IntegerVariable(0), states(states), period(MAX_VALUE / states * states) {
    if (states < 2 || states > MAX_VALUE) {
        throw std::runtime_error("a state counter needs between 2 and 255 states");
    }
}

int64_t StateCounter::state() const {
    return this->integer_value() % this->states;
}

void StateCounter::enter(const int64_t state) {
    this->advance((state - this->state() + this->states) % this->states);
}

void StateCounter::restart(const int64_t state) {
    this->advance((this->states - this->state()) % this->states + state);
}

void StateCounter::advance(const int64_t steps) {
    this->set_integer_value((this->integer_value() + steps) % this->period);
}
