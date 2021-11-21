#include "variable.h"

#include <stdexcept>
#include "expression.h"

void Variable::assign(Expression *expression)
{
    if (this->type == boolean && expression->type == boolean)
    {
        this->boolean_value = expression->evaluate_boolean();
    }
    else if (this->type == integer && expression->type == integer)
    {
        this->integer_value = expression->evaluate_integer();
    }
    else if (this->type == number && expression->is_numbery())
    {
        this->number_value = expression->evaluate_number();
    }
    else if (this->type == string && expression->type == string)
    {
        this->string_value = expression->evaluate_string();
    }
    else
    {
        throw std::runtime_error("type mismatch");
    }
}

BooleanVariable::BooleanVariable(bool value)
{
    this->type = boolean;
    this->boolean_value = value;
}

IntegerVariable::IntegerVariable(int value)
{
    this->type = integer;
    this->integer_value = value;
}

NumberVariable::NumberVariable(double value)
{
    this->type = number;
    this->number_value = value;
}

StringVariable::StringVariable(std::string value)
{
    this->type = string;
    this->string_value = value;
}
