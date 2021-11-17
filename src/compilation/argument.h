#pragma once

#include <string>

enum ArgumentType
{
    integer,
    number,
    identifier,
    string,
};

class Argument
{
public:
    ArgumentType type;
    int integer_value;
    double number_value;
    std::string identifier_value;
    std::string string_value;

    Argument(ArgumentType type);
    static Argument *create_integer(int value);
    static Argument *create_number(double value);
    static Argument *create_identifier(std::string value);
    static Argument *create_string(std::string value);
    bool is_numbery();
    double to_number();
};
