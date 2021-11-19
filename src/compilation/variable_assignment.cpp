#include "variable_assignment.h"

VariableAssignment::VariableAssignment(Variable *variable, Expression *expression)
{
    this->variable = variable;
    this->expression = expression;
}

bool VariableAssignment::run()
{
    switch (this->expression->type)
    {
    case boolean:
        this->variable->set_boolean(this->expression->evaluate_boolean());
        break;
    case integer:
        this->variable->set_integer(this->expression->evaluate_integer());
        break;
    case number:
        this->variable->set_number(this->expression->evaluate_number());
        break;
    case string:
        this->variable->set_string(this->expression->evaluate_string());
        break;
    default:
        printf("error: invalid variable type\n");
    }
    return true;
}