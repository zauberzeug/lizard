#pragma once

#include "action.h"
#include "expression.h"
#include "variable.h"

class VariableAssignment : public Action {
public:
    const Variable_ptr variable;
    const Expression_ptr expression;

    VariableAssignment(const Variable_ptr variable, const Expression_ptr expression);
    ~VariableAssignment();
    bool run();
};