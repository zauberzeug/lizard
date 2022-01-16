#pragma once

#include "action.h"
#include "expression.h"
#include "routine.h"
#include <memory>

class Rule;
using Rule_ptr = std::shared_ptr<Rule>;

class Rule {
public:
    const Expression_ptr condition;
    const Routine_ptr routine;
    Rule(const Expression_ptr condition, const Routine_ptr routine);
};