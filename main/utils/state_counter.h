#pragma once

#include "variable.h"
#include <memory>

class StateCounter;
using StateCounter_ptr = std::shared_ptr<StateCounter>;

// An integer property that counts the transitions of a state machine, so a host that misses frames still sees them.
// The value modulo `states` is the current state, 0 is idle, and it wraps at `period`, the largest multiple of
// `states` up to 255. Every accepted command takes the counter exactly `states` steps from the idle before it to
// its own end, also when it cuts a running cycle short; a refused command leaves it unchanged.
class StateCounter : public IntegerVariable {
public:
    const int64_t states;
    const int64_t period;

    StateCounter(const int64_t states);
    int64_t state() const;
    // A transition the state machine makes by itself; staying in `state` counts nothing.
    void enter(const int64_t state);
    // An accepted command that starts a cycle in `state`: counts up to the next idle first.
    void restart(const int64_t state);

private:
    void advance(const int64_t steps);
};
