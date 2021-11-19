#include "variable.h"

void Variable::set_boolean(bool value)
{
    this->type = boolean;
    this->boolean_value = value;
}

void Variable::set_integer(int value)
{
    this->type = integer;
    this->integer_value = value;
}

void Variable::set_number(double value)
{
    this->type = number;
    this->number_value = value;
}

void Variable::set_string(std::string value)
{
    this->type = string;
    this->string_value = value;
}
