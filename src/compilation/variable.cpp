#include "variable.h"
#include "expression.h"
#include <stdexcept>

Variable::Variable(const Type type) : type(type) {
}

void Variable::assign(const Expression_ptr expression) {
    if (this->type == boolean && expression->type == boolean) {
        this->boolean_value = expression->evaluate_boolean();
    } else if (this->type == integer && expression->type == integer) {
        this->integer_value = expression->evaluate_integer();
    } else if (this->type == number && expression->is_numbery()) {
        this->number_value = expression->evaluate_number();
    } else if (this->type == string && expression->type == string) {
        this->string_value = expression->evaluate_string();
    } else if (this->type == identifier && expression->type == identifier) {
        throw std::runtime_error("assignment of identifiers is forbidden");
    } else {
        throw std::runtime_error("type mismatch for variable assignment");
    }
}

int Variable::print_to_buffer(char *const buffer) const {
    switch (this->type) {
    case boolean:
        return sprintf(buffer, "%s", this->boolean_value ? "true" : "false");
    case integer:
        return sprintf(buffer, "%lld", this->integer_value);
    case number:
        return sprintf(buffer, "%f", this->number_value);
    case string:
        return sprintf(buffer, "\"%s\"", this->string_value.c_str());
    case identifier:
        return sprintf(buffer, "%s", this->identifier_value.c_str());
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
    this->string_value = value;
}

IdentifierVariable::IdentifierVariable(std::string value) : Variable(identifier) {
    this->identifier_value = value;
}
