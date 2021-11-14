#pragma once

#include <list>
#include "action.h"

class Routine
{
private:
    std::list<Action *> actions;

public:
    Routine(std::list<Action *> actions);
    void run();
};