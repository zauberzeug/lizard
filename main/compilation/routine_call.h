#pragma once

#include "action.h"
#include "routine.h"

class RoutineCall : public Action {
public:
    const Routine_ptr routine;

    RoutineCall(const Routine_ptr routine);
    bool run() override;
};
