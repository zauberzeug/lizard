#pragma once

#include "action.h"
#include "routine.h"

class AwaitRoutine : public Action {
private:
    bool is_waiting = false;

public:
    const Routine_ptr routine;

    AwaitRoutine(const Routine_ptr routine);
    bool run() override;
};