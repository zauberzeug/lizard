#include "variable.h"
#include "../utils/string_utils.h"
#include "expression.h"
#include <stdexcept>

Variable::Variable(const Type type) : type(type) {
    if (type == string || type == identifier) {
        this->text = new std::string();
    } else {
        this->integer_slot = 0; // clears the whole slot, so a boolean or number variable starts at false or 0.0 as well
    }
}

Variable::~Variable() {
    if (this->type == string || this->type == identifier) {
        delete this->text;
    }
}

void Variable::expect(const Type type) const {
    if (this->type != type) {
        throw std::runtime_error(std::string("variable is not ") + (type == boolean      ? "a boolean"
                                                                    : type == integer    ? "an integer"
                                                                    : type == number     ? "a number"
                                                                    : type == string     ? "a string"
                                                                    : type == identifier ? "an identifier"
                                                                                         : "of that type"));
    }
}

bool Variable::boolean_value() const {
    this->expect(boolean);
    return this->boolean_slot;
}

int64_t Variable::integer_value() const {
    this->expect(integer);
    return this->integer_slot;
}

double Variable::number_value() const {
    this->expect(number);
    return this->number_slot;
}

const std::string &Variable::string_value() const {
    this->expect(string);
    return *this->text;
}

const std::string &Variable::identifier_value() const {
    this->expect(identifier);
    return *this->text;
}

void Variable::set_boolean_value(const bool value) {
    this->expect(boolean);
    this->boolean_slot = value;
}

void Variable::set_integer_value(const int64_t value) {
    this->expect(integer);
    this->integer_slot = value;
}

void Variable::set_number_value(const double value) {
    this->expect(number);
    this->number_slot = value;
}

void Variable::set_string_value(const std::string &value) {
    this->expect(string);
    *this->text = value;
}

void Variable::assign(const ConstExpression_ptr expression) {
    if (this->type == boolean && expression->type == boolean) {
        this->boolean_slot = expression->evaluate_boolean();
    } else if (this->type == integer && expression->type == integer) {
        this->integer_slot = expression->evaluate_integer();
    } else if (this->type == number && expression->is_numbery()) {
        this->number_slot = expression->evaluate_number();
    } else if (this->type == string && expression->type == string) {
        *this->text = expression->evaluate_string();
    } else if (this->type == identifier && expression->type == identifier) {
        throw std::runtime_error("assignment of identifiers is forbidden");
    } else {
        throw std::runtime_error("type mismatch for variable assignment");
    }
}

int Variable::print_to_buffer(char *const buffer, size_t buffer_len) const {
    switch (this->type) {
    case boolean:
        return csprintf(buffer, buffer_len, "%s", this->boolean_slot ? "true" : "false");
    case integer:
        return csprintf(buffer, buffer_len, "%lld", this->integer_slot);
    case number:
        return csprintf(buffer, buffer_len, "%f", this->number_slot);
    case string:
        return csprintf(buffer, buffer_len, "\"%s\"", this->text->c_str());
    case identifier:
        return csprintf(buffer, buffer_len, "%s", this->text->c_str());
    default:
        throw std::runtime_error("variable has an invalid datatype");
    }
}

BooleanVariable::BooleanVariable(const bool value) : Variable(boolean) {
    this->boolean_slot = value;
}

IntegerVariable::IntegerVariable(const int64_t value) : Variable(integer) {
    this->integer_slot = value;
}

NumberVariable::NumberVariable(double value) : Variable(number) {
    this->number_slot = value;
}

StringVariable::StringVariable(std::string value) : Variable(string) {
    *this->text = value;
}

IdentifierVariable::IdentifierVariable(std::string value) : Variable(identifier) {
    *this->text = value;
}
