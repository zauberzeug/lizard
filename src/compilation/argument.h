#pragma once

#include <string>

enum ArgumentType
{
    number,
    identifier,
};

class Argument
{
public:
    ArgumentType type;
    double number_value;
    std::string identifier_value;
    Argument(double value);
    Argument(std::string value);
};
