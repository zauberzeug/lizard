#pragma once

#include "action.h"
#include <memory>
#include <vector>

class Routine;
using Routine_ptr = std::shared_ptr<Routine>;

class Routine {
private:
    const std::vector<Action_ptr> actions;
    int instruction_index = -1;
    bool failed = false;

public:
    Routine(const std::vector<Action_ptr> actions);
    bool is_running() const;
    bool has_failed() const;
    void start();
    void step();
    void fail();
};
