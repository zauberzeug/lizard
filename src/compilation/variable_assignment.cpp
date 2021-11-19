#include "variable_assignment.h"

VariableAssignment::VariableAssignment(Variable *variable, Expression *expression)
{
    this->variable = variable;
    this->expression = expression;
}

bool VariableAssignment::run()
{
    if (this->expression->type == number)
    {
        this->variable->set_number(this->expression->evaluate_number());
    }
    else
    {
        printf("error: only number variables are supported yet\n");
    }
    return true;
}