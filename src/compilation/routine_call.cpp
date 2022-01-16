#include "routine_call.h"

RoutineCall::RoutineCall(const Routine_ptr routine) : routine(routine) {
}

bool RoutineCall::run() {
    this->routine->start();
    return true;
}