#include "argument.h"

Argument::Argument(double value)
{
    this->type = number;
    this->number_value = value;
}

Argument::Argument(std::string value)
{
    this->type = identifier;
    this->identifier_value = value;
}
