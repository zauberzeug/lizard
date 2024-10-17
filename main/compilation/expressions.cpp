#include "expressions.h"
#include "../modules/module.h"
#include "../utils/string_utils.h"
#include "math.h"
#include <stdexcept>

int write_arguments_to_buffer(const std::vector<ConstExpression_ptr> arguments, char *buffer, size_t buffer_len) {
    int pos = 0;
    for (auto const &argument : arguments) {
        if (argument != arguments[0]) {
            pos += csprintf(&buffer[pos], buffer_len - pos, ", ");
        }
        pos += argument->print_to_buffer(&buffer[pos], buffer_len - pos);
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
    if (this->type == boolean)
        return this->variable->boolean_value;
    throw std::runtime_error("variable is not a boolean");
}

int64_t VariableExpression::evaluate_integer() const {
    if (this->type == integer)
        return this->variable->integer_value;
    if (this->type == boolean)
        return this->variable->boolean_value ? 1 : 0;
    throw std::runtime_error("variable cannot evaluate to an integer");
}

double VariableExpression::evaluate_number() const {
    if (this->type == number)
        return this->variable->number_value;
    if (this->type == integer)
        return this->variable->integer_value;
    if (this->type == boolean)
        return this->variable->boolean_value ? 1.0 : 0.0;
    throw std::runtime_error("variable cannot evaluate to a number");
}

std::string VariableExpression::evaluate_string() const {
    if (this->type == string)
        return this->variable->string_value;
    throw std::runtime_error("variable is not a string");
}

std::string VariableExpression::evaluate_identifier() const {
    if (this->type == identifier)
        return this->variable->identifier_value;
    throw std::runtime_error("variable is not an identifier");
}

PropertyExpression::PropertyExpression(const ConstModule_ptr module, const std::string property_name)
    : Expression(module->get_property(property_name)->type), module(module), property_name(property_name) {
}

bool PropertyExpression::evaluate_boolean() const {
    if (this->type == boolean)
        return this->module->get_property(this->property_name)->boolean_value;
    throw std::runtime_error("property is not a boolean");
}

int64_t PropertyExpression::evaluate_integer() const {
    if (this->type == integer)
        return this->module->get_property(this->property_name)->integer_value;
    if (this->type == boolean)
        return this->module->get_property(this->property_name)->boolean_value ? 1 : 0;
    throw std::runtime_error("property cannot evaluate to an integer");
}

double PropertyExpression::evaluate_number() const {
    if (this->type == number)
        return this->module->get_property(this->property_name)->number_value;
    if (this->type == integer)
        return this->module->get_property(this->property_name)->integer_value;
    if (this->type == boolean)
        return this->module->get_property(this->property_name)->boolean_value ? 1.0 : 0.0;
    throw std::runtime_error("property cannot evaluate to a number");
}

std::string PropertyExpression::evaluate_string() const {
    if (this->type == string)
        return this->module->get_property(this->property_name)->string_value;
    throw std::runtime_error("property is not a string");
}

std::string PropertyExpression::evaluate_identifier() const {
    if (this->type == identifier)
        return this->module->get_property(this->property_name)->identifier_value;
    throw std::runtime_error("property is not an identifier");
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
