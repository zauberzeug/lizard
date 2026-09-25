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

// A variable holds exactly one value of its type. The value shares one 8-byte slot, so writing a field of another
// type overwrites the value instead of landing in a separate, never-read field; string and identifier variables
// keep their text on the heap behind the same slot. Every property of every module is a Variable, so the slot
// keeps each of them at 16 bytes instead of the 72 that separate fields for all five types took.
class Variable {
public:
    const Type type;
    union {
        bool boolean_value;
        int64_t integer_value;
        double number_value;
        std::string *text; // owned by the variable; only string and identifier variables use the slot this way,
                           // and only through string_value(), identifier_value() and set_string_value()
    };

    Variable(const Type type);
    ~Variable();
    Variable(const Variable &) = delete;
    Variable &operator=(const Variable &) = delete;

    void assign(const ConstExpression_ptr expression);
    int print_to_buffer(char *const buffer, size_t buffer_len) const;

    // the text of a string or identifier variable; throws for any other type
    const std::string &string_value() const;
    const std::string &identifier_value() const;
    void set_string_value(const std::string &value);
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
