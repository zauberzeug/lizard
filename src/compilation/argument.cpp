#include "argument.h"

Argument::Argument(ArgumentType type)
{
    this->type = type;
}

Argument *Argument::create_integer(int value)
{
    Argument *argument = new Argument(integer);
    argument->integer_value = value;
    return argument;
}

Argument *Argument::create_number(double value)
{
    Argument *argument = new Argument(number);
    argument->number_value = value;
    return argument;
}

Argument *Argument::create_identifier(std::string value)
{
    Argument *argument = new Argument(identifier);
    argument->identifier_value = value;
    return argument;
}

Argument *Argument::create_string(std::string value)
{
    Argument *argument = new Argument(string);
    argument->string_value = value;
    return argument;
}

bool Argument::is_numbery()
{
    return this->type == number || this->type == integer;
}

double Argument::to_number()
{
    if (this->type == number)
    {
        return this->number_value;
    }
    else if (this->type == integer)
    {
        return this->integer_value;
    }
    else
    {
        printf("error: argument can not be converted to number\n");
        return 0;
    }
}