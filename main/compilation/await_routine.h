#pragma once

#include "action.h"
#include "routine.h"
#include <string>

class AwaitRoutine : public Action {
private:
    bool is_waiting = false;

public:
    const Routine_ptr routine;
    const std::string routine_name;

    AwaitRoutine(const Routine_ptr routine, const std::string routine_name);
    bool run() override;
};
