#pragma once

#include "action.h"
#include "expression.h"
#include "variable.h"

class VariableAssignment : public Action
{
public:
    Variable *const variable;
    const Expression *const expression;

    VariableAssignment(Variable *const variable, const Expression *const expression);
    ~VariableAssignment();
    bool run();
};