#pragma once

#include <string>

enum ArgumentType
{
    integer,
    string,
};

class Argument
{
public:
    ArgumentType type;
    int integer_value;
    std::string string_value;
    Argument(int value);
    Argument(std::string value);
};
