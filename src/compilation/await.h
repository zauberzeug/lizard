#pragma once

#include "action.h"
#include "condition.h"

class Await : public Action
{
public:
    Condition *condition;

    Await(Condition *condition);
    bool run();
};