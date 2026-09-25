#pragma once

#include "type.h"
#include <memory>
#include <string>

class Expression; // NOTE: forward declaration to avoid cyclic include
using Expression_ptr = std::shared_ptr<Expression>;
using ConstExpression_ptr = std::shared_ptr<const Expression>;

class Variable;
using Variable_ptr = std::shared_ptr<Variable>;
using ConstVariable_ptr = std::shared_ptr<const Variable>;

class Variable {
public:
    const Type type;
    bool boolean_value;
    int64_t integer_value;
    double number_value;

    Variable(const Type type);
    void assign(const ConstExpression_ptr expression);
    int print_to_buffer(char *const buffer, size_t buffer_len) const;

    // the text of a string or identifier variable; throws for any other type
    const std::string &string_value() const;
    const std::string &identifier_value() const;
    void set_string_value(const std::string &value);

protected:
    // Only string and identifier variables own a std::string. An empty std::string costs 24 bytes on the ESP32,
    // and every property of every module is a Variable, so two of them per numeric variable added up to a third
    // of each variable's heap footprint.
    std::unique_ptr<std::string> text;
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
