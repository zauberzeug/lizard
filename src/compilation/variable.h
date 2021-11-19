#pragma once

#include <string>
#include "type.h"

class Variable
{
public:
    bool boolean_value;
    int integer_value;
    double number_value;
    std::string string_value;
    Type type;

    void set_boolean(bool value);
    void set_integer(int value);
    void set_number(double value);
    void set_string(std::string value);
};