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
    BooleanVariable();
};

class IntegerVariable : public Variable
{
public:
    IntegerVariable();
};

class NumberVariable : public Variable
{
public:
    NumberVariable();
};

class StringVariable : public Variable
{
public:
    StringVariable();
};
