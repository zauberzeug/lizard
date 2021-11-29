#include "variable_assignment.h"

VariableAssignment::VariableAssignment(Variable *variable, Expression *expression)
{
    this->variable = variable;
    this->expression = expression;
}

VariableAssignment::~VariableAssignment()
{
    // NOTE: don't delete globally managed variables
    delete this->expression;
}

bool VariableAssignment::run()
{
    this->variable->assign(this->expression);
    return true;
}