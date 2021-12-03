#pragma once

#include "action.h"
#include "routine.h"

class AwaitRoutine : public Action {
public:
    Routine *const routine;

    AwaitRoutine(Routine *const routine);
    ~AwaitRoutine();
    bool run();
};