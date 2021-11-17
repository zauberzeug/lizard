#include "routine_call.h"

RoutineCall::RoutineCall(Routine *routine)
{
    this->routine = routine;
}

bool RoutineCall::run()
{
    this->routine->start();
    return true;
}