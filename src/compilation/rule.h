#pragma once

#include "action.h"
#include "expression.h"
#include "routine.h"

class Rule {
public:
    const Expression_ptr condition;
    Routine *const routine;
    Rule(const Expression_ptr condition, Routine *const routine);
};