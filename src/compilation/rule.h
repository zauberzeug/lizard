#pragma once

#include "action.h"
#include "expression.h"
#include "routine.h"

class Rule
{
public:
    const Expression *const condition;
    Routine *const routine;
    Rule(const Expression *const condition, Routine *const routine);
};