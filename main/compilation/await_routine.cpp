#include "await_routine.h"
#include <stdexcept>

AwaitRoutine::AwaitRoutine(const Routine_ptr routine, const std::string routine_name)
    : routine(routine), routine_name(routine_name) {
}

bool AwaitRoutine::run() {
    if (!this->routine->is_running() && !this->is_waiting) {
        this->routine->start();
        this->is_waiting = true;
    }
    const bool can_proceed = !this->routine->is_running();
    if (can_proceed) {
        this->is_waiting = false;
        if (this->routine->has_failed()) {
            throw std::runtime_error("awaited routine \"" + this->routine_name + "\" failed");
        }
    }
    return can_proceed;
}
