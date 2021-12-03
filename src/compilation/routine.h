#pragma once

#include "action.h"
#include <vector>

class Routine {
private:
    const std::vector<Action *> actions;
    int instruction_index = -1;

public:
    Routine(const std::vector<Action *> actions);
    ~Routine();
    bool is_running() const;
    void start();
    void step();
};