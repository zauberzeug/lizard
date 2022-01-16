#include "await_routine.h"

AwaitRoutine::AwaitRoutine(const Routine_ptr routine)
    : routine(routine) {
}

bool AwaitRoutine::run() {
    if (!this->routine->is_running()) {
        this->routine->start();
    }
    this->routine->step();
    return !this->routine->is_running();
}