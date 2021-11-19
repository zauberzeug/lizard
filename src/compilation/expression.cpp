#include "expression.h"

bool Expression::evaluate_boolean()
{
    return false; // TODO
}

int Expression::evaluate_integer()
{
    return 0; // TODO
}

double Expression::evaluate_number()
{
    return 0; // TODO
}

std::string Expression::evaluate_identifier()
{
    return ""; // TODO
}

std::string Expression::evaluate_string()
{
    return ""; // TODO
}

bool Expression::is_numbery()
{
    return this->type == number || this->type == integer;
}

IntegerExpression::IntegerExpression(int value)
{
    this->type = integer;
    this->value = value;
}

int IntegerExpression::evaluate_integer()
{
    return this->value;
}

StringExpression::StringExpression(std::string value)
{
    this->type = string;
    this->value = value;
}

std::string StringExpression::evaluate_string()
{
    return this->value;
}