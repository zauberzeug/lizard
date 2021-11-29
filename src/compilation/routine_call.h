#pragma once

#include "action.h"
#include "routine.h"

class RoutineCall : public Action
{
public:
    Routine *routine;

    RoutineCall(Routine *routine);
    ~RoutineCall();
    bool run();
};