#pragma once

#include "action.h"
#include "expression.h"
#include "variable.h"

class VariableAssignment : public Action {
public:
    Variable *const variable;
    const Expression_ptr expression;

    VariableAssignment(Variable *const variable, const Expression_ptr expression);
    ~VariableAssignment();
    bool run();
};