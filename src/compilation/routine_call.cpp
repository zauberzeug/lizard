#include "routine_call.h"

RoutineCall::RoutineCall(const Routine_ptr routine) : routine(routine) {
}

RoutineCall::~RoutineCall() {
    // NOTE: don't delete globally managed routines
}

bool RoutineCall::run() {
    this->routine->start();
    return true;
}