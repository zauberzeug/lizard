#pragma once

#include "action.h"
#include "routine.h"

class AwaitRoutine : public Action
{
public:
    Routine *routine;

    AwaitRoutine(Routine *routine);
    ~AwaitRoutine();
    bool run();
};