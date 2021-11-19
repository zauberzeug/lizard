#pragma once

#include "action.h"
#include "expression.h"
#include "routine.h"

class Rule
{
public:
    Expression *condition;
    Routine *routine;
    Rule(Expression *condition, Routine *routine);
};