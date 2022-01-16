#include "await_routine.h"

AwaitRoutine::AwaitRoutine(const Routine_ptr routine)
    : routine(routine) {
}

AwaitRoutine::~AwaitRoutine() {
    // NOTE: don't delete globally managed routines
}

bool AwaitRoutine::run() {
    if (!this->routine->is_running()) {
        this->routine->start();
    }
    this->routine->step();
    return !this->routine->is_running();
}