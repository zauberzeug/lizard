#include "await_routine.h"

AwaitRoutine::AwaitRoutine(const Routine_ptr routine)
    : routine(routine) {
}

bool AwaitRoutine::run() {
    if (!this->routine->is_running() && !this->is_waiting) {
        this->routine->start();
        this->is_waiting = true;
    }
    const bool can_proceed = !this->routine->is_running();
    if (can_proceed) {
        this->is_waiting = false;
    }
    return can_proceed;
}