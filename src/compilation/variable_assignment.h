#pragma once

#include "action.h"
#include "expression.h"
#include "variable.h"

class VariableAssignment : public Action
{
public:
    Variable *variable;
    Expression *expression;

    VariableAssignment(Variable *variable, Expression *expression);
    ~VariableAssignment();
    bool run();
};