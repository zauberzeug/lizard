#include "variable_assignment.h"

VariableAssignment::VariableAssignment(Variable *variable, Expression *expression)
{
    this->variable = variable;
    this->expression = expression;
}

bool VariableAssignment::run()
{
    this->variable->set(this->expression->evaluate());
    return true;
}