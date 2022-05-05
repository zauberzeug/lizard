#include "expression.h"
#include "math.h"
#include <stdexcept>

int write_arguments_to_buffer(const std::vector<ConstExpression_ptr> arguments, char *buffer) {
    int pos = 0;
    for (auto const &argument : arguments) {
        if (argument != arguments[0]) {
            pos += std::sprintf(&buffer[pos], ", ");
        }
        pos += argument->print_to_buffer(&buffer[pos]);
    }
    return pos;
}

Type get_common_number_type(const ConstExpression_ptr left, const ConstExpression_ptr right) {
    if (left->type == integer && right->type == integer) {
        return integer;
    } else if (left->is_numbery() && right->is_numbery()) {
        return number;
    } else {
        throw std::runtime_error("invalid type for arithmetic operation");
    }
}

void check_number_types(const ConstExpression_ptr left, const ConstExpression_ptr right) {
    if (!left->is_numbery() || !right->is_numbery()) {
        throw std::runtime_error("invalid type for comparison");
    }
}

void check_boolean_types(const ConstExpression_ptr left, const ConstExpression_ptr right) {
    if (left->type != boolean || right->type != boolean) {
        throw std::runtime_error("invalid type for logical operation");
    }
}

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

int Expression::print_to_buffer(char *buffer) const {
    switch (this->type) {
    case boolean:
        return sprintf(buffer, "%s", this->evaluate_boolean() ? "true" : "false");
    case integer:
        return sprintf(buffer, "%lld", this->evaluate_integer());
    case number:
        return sprintf(buffer, "%f", this->evaluate_number());
    case string:
        return sprintf(buffer, "\"%s\"", this->evaluate_string().c_str());
    case identifier:
        return sprintf(buffer, "%s", this->evaluate_identifier().c_str());
    default:
        throw std::runtime_error("expression has an invalid datatype");
    }
}

BooleanExpression::BooleanExpression(bool value)
    : Expression(boolean), value(value) {
}

bool BooleanExpression::evaluate_boolean() const {
    return this->value;
}

StringExpression::StringExpression(std::string value)
    : Expression(string), value(value) {
}

std::string StringExpression::evaluate_string() const {
    return this->value;
}

IntegerExpression::IntegerExpression(int64_t value)
    : Expression(integer), value(value) {
}

int64_t IntegerExpression::evaluate_integer() const {
    return this->value;
}

double IntegerExpression::evaluate_number() const {
    return this->value;
}

NumberExpression::NumberExpression(double value)
    : Expression(number), value(value) {
}

double NumberExpression::evaluate_number() const {
    return this->value;
}

VariableExpression::VariableExpression(const ConstVariable_ptr variable)
    : Expression(variable->type), variable(variable) {
}

bool VariableExpression::evaluate_boolean() const {
    if (this->type != boolean) {
        throw std::runtime_error("variable is not a boolean");
    }
    return this->variable->boolean_value;
}

int64_t VariableExpression::evaluate_integer() const {
    if (this->type != integer) {
        throw std::runtime_error("variable is not an integer");
    }
    return this->variable->integer_value;
}

double VariableExpression::evaluate_number() const {
    if (!this->is_numbery()) {
        throw std::runtime_error("variable is not a number");
    }
    return this->type == number ? this->variable->number_value : this->variable->integer_value;
}

std::string VariableExpression::evaluate_string() const {
    if (this->type != string) {
        throw std::runtime_error("variable is not a string");
    }
    return this->variable->string_value;
}

std::string VariableExpression::evaluate_identifier() const {
    if (this->type != identifier) {
        throw std::runtime_error("variable is not an identifier");
    }
    return this->variable->identifier_value;
}

PowerExpression::PowerExpression(const ConstExpression_ptr left, const ConstExpression_ptr right)
    : Expression(get_common_number_type(left, right)), left(left), right(right) {
}

int64_t PowerExpression::evaluate_integer() const {
    return pow(this->left->evaluate_integer(), this->right->evaluate_integer());
}

double PowerExpression::evaluate_number() const {
    return pow(this->left->evaluate_number(), this->right->evaluate_number());
}

NegateExpression::NegateExpression(const ConstExpression_ptr operand)
    : Expression(get_common_number_type(operand, operand)), operand(operand) {
}

int64_t NegateExpression::evaluate_integer() const {
    return -this->operand->evaluate_integer();
}

double NegateExpression::evaluate_number() const {
    return -this->operand->evaluate_number();
}

MultiplyExpression::MultiplyExpression(const ConstExpression_ptr left, const ConstExpression_ptr right)
    : Expression(get_common_number_type(left, right)), left(left), right(right) {
}

int64_t MultiplyExpression::evaluate_integer() const {
    return this->left->evaluate_integer() * this->right->evaluate_integer();
}

double MultiplyExpression::evaluate_number() const {
    return this->left->evaluate_number() * this->right->evaluate_number();
}

DivideExpression::DivideExpression(const ConstExpression_ptr left, const ConstExpression_ptr right)
    : Expression(get_common_number_type(left, right)), left(left), right(right) {
}

int64_t DivideExpression::evaluate_integer() const {
    return this->left->evaluate_integer() / this->right->evaluate_integer();
}

double DivideExpression::evaluate_number() const {
    return this->left->evaluate_number() / this->right->evaluate_number();
}

ModuloExpression::ModuloExpression(const ConstExpression_ptr left, const ConstExpression_ptr right)
    : Expression(get_common_number_type(left, right)), left(left), right(right) {
}

int64_t ModuloExpression::evaluate_integer() const {
    return this->left->evaluate_integer() % this->right->evaluate_integer();
}

double ModuloExpression::evaluate_number() const {
    return fmod(this->left->evaluate_number(), this->right->evaluate_number());
}

FloorDivideExpression::FloorDivideExpression(const ConstExpression_ptr left, const ConstExpression_ptr right)
    : Expression(get_common_number_type(left, right)), left(left), right(right) {
}

int64_t FloorDivideExpression::evaluate_integer() const {
    return this->left->evaluate_integer() / this->right->evaluate_integer();
}

double FloorDivideExpression::evaluate_number() const {
    return floor(this->left->evaluate_number() / this->right->evaluate_number());
}

AddExpression::AddExpression(const ConstExpression_ptr left, const ConstExpression_ptr right)
    : Expression(get_common_number_type(left, right)), left(left), right(right) {
}

int64_t AddExpression::evaluate_integer() const {
    return this->left->evaluate_integer() + this->right->evaluate_integer();
}

double AddExpression::evaluate_number() const {
    return this->left->evaluate_number() + this->right->evaluate_number();
}

SubtractExpression::SubtractExpression(const ConstExpression_ptr left, const ConstExpression_ptr right)
    : Expression(get_common_number_type(left, right)), left(left), right(right) {
}

int64_t SubtractExpression::evaluate_integer() const {
    return this->left->evaluate_integer() - this->right->evaluate_integer();
}

double SubtractExpression::evaluate_number() const {
    return this->left->evaluate_number() - this->right->evaluate_number();
}

ShiftLeftExpression::ShiftLeftExpression(const ConstExpression_ptr left, const ConstExpression_ptr right)
    : Expression(integer), left(left), right(right) {
}

int64_t ShiftLeftExpression::evaluate_integer() const {
    return this->left->evaluate_integer() << this->right->evaluate_integer();
}

ShiftRightExpression::ShiftRightExpression(const ConstExpression_ptr left, const ConstExpression_ptr right)
    : Expression(integer), left(left), right(right) {
}

int64_t ShiftRightExpression::evaluate_integer() const {
    return this->left->evaluate_integer() >> this->right->evaluate_integer();
}

BitAndExpression::BitAndExpression(const ConstExpression_ptr left, const ConstExpression_ptr right)
    : Expression(integer), left(left), right(right) {
}

int64_t BitAndExpression::evaluate_integer() const {
    return this->left->evaluate_integer() & this->right->evaluate_integer();
}

BitXorExpression::BitXorExpression(const ConstExpression_ptr left, const ConstExpression_ptr right)
    : Expression(integer), left(left), right(right) {
}

int64_t BitXorExpression::evaluate_integer() const {
    return this->left->evaluate_integer() ^ this->right->evaluate_integer();
}

BitOrExpression::BitOrExpression(const ConstExpression_ptr left, const ConstExpression_ptr right)
    : Expression(integer), left(left), right(right) {
}

int64_t BitOrExpression::evaluate_integer() const {
    return this->left->evaluate_integer() | this->right->evaluate_integer();
}

GreaterExpression::GreaterExpression(const ConstExpression_ptr left, const ConstExpression_ptr right)
    : Expression(boolean), left(left), right(right) {
    check_number_types(left, right);
}

bool GreaterExpression::evaluate_boolean() const {
    return this->left->evaluate_number() > this->right->evaluate_number();
}

LessExpression::LessExpression(const ConstExpression_ptr left, const ConstExpression_ptr right)
    : Expression(boolean), left(left), right(right) {
    check_number_types(left, right);
}

bool LessExpression::evaluate_boolean() const {
    return this->left->evaluate_number() < this->right->evaluate_number();
}

GreaterEqualExpression::GreaterEqualExpression(const ConstExpression_ptr left, const ConstExpression_ptr right)
    : Expression(boolean), left(left), right(right) {
    check_number_types(left, right);
}

bool GreaterEqualExpression::evaluate_boolean() const {
    return this->left->evaluate_number() >= this->right->evaluate_number();
}

LessEqualExpression::LessEqualExpression(const ConstExpression_ptr left, const ConstExpression_ptr right)
    : Expression(boolean), left(left), right(right) {
    check_number_types(left, right);
}

bool LessEqualExpression::evaluate_boolean() const {
    return this->left->evaluate_number() <= this->right->evaluate_number();
}

EqualExpression::EqualExpression(const ConstExpression_ptr left, const ConstExpression_ptr right)
    : Expression(boolean), left(left), right(right) {
    check_number_types(left, right);
}

bool EqualExpression::evaluate_boolean() const {
    return this->left->evaluate_number() == this->right->evaluate_number();
}

UnequalExpression::UnequalExpression(const ConstExpression_ptr left, const ConstExpression_ptr right)
    : Expression(boolean), left(left), right(right) {
    check_number_types(left, right);
}

bool UnequalExpression::evaluate_boolean() const {
    return this->left->evaluate_number() != this->right->evaluate_number();
}

NotExpression::NotExpression(const ConstExpression_ptr operand)
    : Expression(boolean), operand(operand) {
    check_boolean_types(operand, operand);
}

bool NotExpression::evaluate_boolean() const {
    return !this->operand->evaluate_boolean();
}

AndExpression::AndExpression(const ConstExpression_ptr left, const ConstExpression_ptr right)
    : Expression(boolean), left(left), right(right) {
    check_boolean_types(left, right);
}

bool AndExpression::evaluate_boolean() const {
    return this->left->evaluate_boolean() && this->right->evaluate_boolean();
}

OrExpression::OrExpression(const ConstExpression_ptr left, const ConstExpression_ptr right)
    : Expression(boolean), left(left), right(right) {
    check_boolean_types(left, right);
}

bool OrExpression::evaluate_boolean() const {
    return this->left->evaluate_boolean() || this->right->evaluate_boolean();
}
