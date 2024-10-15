#include "expression.h"
#include <stdexcept>

Expression::Expression(const Type type) : type(type) {
}

bool Expression::evaluate_boolean() const {
    throw std::runtime_error("not implemented");
}

int64_t Expression::evaluate_integer() const {
    return evaluate_boolean() ? 1 : 0;
}

double Expression::evaluate_number() const {
    return evaluate_integer();
}

std::string Expression::evaluate_identifier() const {
    throw std::runtime_error("not implemented");
}

std::string Expression::evaluate_string() const {
    throw std::runtime_error("not implemented");
}

bool Expression::is_numbery() const {
    return this->type == number || this->type == integer || this->type == boolean;
}

int Expression::print_to_buffer(char *buffer, size_t buffer_len) const {
    switch (this->type) {
    case boolean:
        return csprintf(buffer, buffer_len, "%s", this->evaluate_boolean() ? "true" : "false");
    case integer:
        return csprintf(buffer, buffer_len,  "%lld", this->evaluate_integer());
    case number:
        return csprintf(buffer, buffer_len,  "%f", this->evaluate_number());
    case string:
        return csprintf(buffer, buffer_len,  "\"%s\"", this->evaluate_string().c_str());
    case identifier:
        return csprintf(buffer, buffer_len,  "%s", this->evaluate_identifier().c_str());
    default:
        throw std::runtime_error("expression has an invalid datatype");
    }
}
