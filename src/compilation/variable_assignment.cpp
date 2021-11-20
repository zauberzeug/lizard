#include "variable_assignment.h"

VariableAssignment::VariableAssignment(Variable *variable, Expression *expression)
{
    this->variable = variable;
    this->expression = expression;
}

bool VariableAssignment::run()
{
    this->variable->assign(this->expression);
    return true;
}