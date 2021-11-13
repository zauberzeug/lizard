#pragma once

#include <list>
#include "action.h"

class Routine
{
public:
    std::list<Action *> actions;
};