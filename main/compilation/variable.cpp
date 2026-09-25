#include "variable.h"
#include "../utils/string_utils.h"
#include "expression.h"
#include <stdexcept>

Variable::Variable(const Type type) : type(type) {
    if (type == string || type == identifier) {
        this->text = new std::string();
    } else {
        this->integer_value = 0; // clears the whole slot, so a boolean or number variable starts at false or 0.0 as well
    }
}

Variable::~Variable() {
    if (this->type == string || this->type == identifier) {
        delete this->text;
    }
}

const std::string &Variable::string_value() const {
    if (this->type != string) {
        throw std::runtime_error("variable is not a string");
    }
    return *this->text;
}

const std::string &Variable::identifier_value() const {
    if (this->type != identifier) {
        throw std::runtime_error("variable is not an identifier");
    }
    return *this->text;
}

void Variable::set_string_value(const std::string &value) {
    if (this->type != string) {
        throw std::runtime_error("variable is not a string");
    }
    *this->text = value;
}

void Variable::assign(const ConstExpression_ptr expression) {
    if (this->type == boolean && expression->type == boolean) {
        this->boolean_value = expression->evaluate_boolean();
    } else if (this->type == integer && expression->type == integer) {
        this->integer_value = expression->evaluate_integer();
    } else if (this->type == number && expression->is_numbery()) {
        this->number_value = expression->evaluate_number();
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
        return csprintf(buffer, buffer_len, "%s", this->boolean_value ? "true" : "false");
    case integer:
        return csprintf(buffer, buffer_len, "%lld", this->integer_value);
    case number:
        return csprintf(buffer, buffer_len, "%f", this->number_value);
    case string:
        return csprintf(buffer, buffer_len, "\"%s\"", this->text->c_str());
    case identifier:
        return csprintf(buffer, buffer_len, "%s", this->text->c_str());
    default:
        throw std::runtime_error("variable has an invalid datatype");
    }
}

BooleanVariable::BooleanVariable(const bool value) : Variable(boolean) {
    this->boolean_value = value;
}

IntegerVariable::IntegerVariable(const int64_t value) : Variable(integer) {
    this->integer_value = value;
}

NumberVariable::NumberVariable(double value) : Variable(number) {
    this->number_value = value;
}

StringVariable::StringVariable(std::string value) : Variable(string) {
    *this->text = value;
}

IdentifierVariable::IdentifierVariable(std::string value) : Variable(identifier) {
    *this->text = value;
}
