#pragma once

#include "type.h"
#include <memory>
#include <string>

class Expression; // NOTE: forward declaration to avoid cyclic include
using Expression_ptr = std::shared_ptr<Expression>;

class Variable {
public:
    const Type type;
    bool boolean_value;
    int64_t integer_value;
    double number_value;
    std::string string_value;
    std::string identifier_value;

    Variable(const Type type);
    void assign(const Expression_ptr expression);
    int print_to_buffer(char *const buffer) const;
};

class BooleanVariable : public Variable {
public:
    BooleanVariable(const bool value = false);
};

class IntegerVariable : public Variable {
public:
    IntegerVariable(const int64_t value = 0);
};

class NumberVariable : public Variable {
public:
    NumberVariable(const double value = 0.0);
};

class StringVariable : public Variable {
public:
    StringVariable(const std::string value = "");
};

class IdentifierVariable : public Variable {
public:
    IdentifierVariable(const std::string value = "");
};
