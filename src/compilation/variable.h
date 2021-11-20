#pragma once

#include <string>
#include "type.h"

class Expression; // NOTE: forward declaration to avoid cyclic include

class Variable
{
public:
    Type type;
    bool boolean_value;
    int integer_value;
    double number_value;
    std::string string_value;

    void assign(Expression *expression);
};

class BooleanVariable : public Variable
{
    BooleanVariable(bool value);
};

class IntegerVariable : public Variable
{
    IntegerVariable(int value);
};

class NumberVariable : public Variable
{
    NumberVariable(double value);
};

class StringVariable : public Variable
{
    StringVariable(std::string value);
};
