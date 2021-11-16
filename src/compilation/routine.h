#pragma once

#include <vector>
#include "action.h"

class Routine
{
private:
    std::vector<Action *> actions;
    unsigned int instruction_index = -1;

public:
    Routine(std::vector<Action *> actions);
    bool is_running();
    void start();
    void step();
};