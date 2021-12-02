#include "routine_call.h"

RoutineCall::RoutineCall(Routine *const routine) : routine(routine)
{
}

RoutineCall::~RoutineCall()
{
    // NOTE: don't delete globally managed routines
}

bool RoutineCall::run()
{
    this->routine->start();
    return true;
}