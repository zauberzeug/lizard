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
public:
    BooleanVariable(bool value = false);
};

class IntegerVariable : public Variable
{
public:
    IntegerVariable(int value = 0);
};

class NumberVariable : public Variable
{
public:
    NumberVariable(double value = 0.0);
};

class StringVariable : public Variable
{
public:
    StringVariable(std::string value = "");
};
