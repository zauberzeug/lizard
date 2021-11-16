#include "argument.h"

Argument::Argument(int value)
{
    this->type = integer;
    this->integer_value = value;
}

Argument::Argument(std::string value)
{
    this->type = integer;
    this->string_value = value;
}
