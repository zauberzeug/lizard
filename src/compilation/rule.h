#pragma once

#include "action.h"
#include "condition.h"
#include "routine.h"

class Rule
{
public:
    Condition *condition;
    Routine *routine;
    Rule(Condition *condition, Routine *routine);
};