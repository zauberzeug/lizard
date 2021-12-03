#pragma once

#include "action.h"
#include "routine.h"

class RoutineCall : public Action {
public:
    Routine *const routine;

    RoutineCall(Routine *const routine);
    ~RoutineCall();
    bool run();
};