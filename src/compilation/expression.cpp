#include "expression.h"
#include "math.h"
#include <stdexcept>

Type get_common_number_type(const Expression_ptr left, const Expression_ptr right) {
    if (left->type == integer && right->type == integer) {
        return integer;
    } else if (left->is_numbery() && right->is_numbery()) {
        return number;
    } else {
        throw std::runtime_error("invalid type for arithmetic operation");
    }
}

void check_number_types(const Expression_ptr left, const Expression_ptr right) {
    if (!left->is_numbery() || !right->is_numbery()) {
        throw std::runtime_error("invalid type for comparison");
    }
}

void check_boolean_types(const Expression_ptr left, const Expression_ptr right) {
    if (left->type != boolean || right->type != boolean) {
        throw std::runtime_error("invalid type for logical operation");
    }
}

Expression::Expression(const Type type) : type(type) {
}

Expression::~Expression() {
}

bool Expression::evaluate_boolean() const {
    throw std::runtime_error("not implemented");
}

int64_t Expression::evaluate_integer() const {
    throw std::runtime_error("not implemented");
}

double Expression::evaluate_number() const {
    throw std::runtime_error("not implemented");
}

std::string Expression::evaluate_identifier() const {
    throw std::runtime_error("not implemented");
}

std::string Expression::evaluate_string() const {
    throw std::runtime_error("not implemented");
}

bool Expression::is_numbery() const {
    return this->type == number || this->type == integer;
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

VariableExpression::VariableExpression(const Variable_ptr variable)
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

PowerExpression::PowerExpression(const Expression_ptr left, const Expression_ptr right)
    : Expression(get_common_number_type(left, right)), left(left), right(right) {
}

PowerExpression::~PowerExpression() {
}

int64_t PowerExpression::evaluate_integer() const {
    return pow(this->left->evaluate_integer(), this->right->evaluate_integer());
}

double PowerExpression::evaluate_number() const {
    return pow(this->left->evaluate_number(), this->right->evaluate_number());
}

NegateExpression::NegateExpression(const Expression_ptr operand)
    : Expression(get_common_number_type(operand, operand)), operand(operand) {
}

NegateExpression::~NegateExpression() {
}

int64_t NegateExpression::evaluate_integer() const {
    return -this->operand->evaluate_integer();
}

double NegateExpression::evaluate_number() const {
    return -this->operand->evaluate_number();
}

MultiplyExpression::MultiplyExpression(const Expression_ptr left, const Expression_ptr right)
    : Expression(get_common_number_type(left, right)), left(left), right(right) {
}

MultiplyExpression::~MultiplyExpression() {
}

int64_t MultiplyExpression::evaluate_integer() const {
    return this->left->evaluate_integer() * this->right->evaluate_integer();
}

double MultiplyExpression::evaluate_number() const {
    return this->left->evaluate_number() * this->right->evaluate_number();
}

DivideExpression::DivideExpression(const Expression_ptr left, const Expression_ptr right)
    : Expression(get_common_number_type(left, right)), left(left), right(right) {
}

DivideExpression::~DivideExpression() {
}

int64_t DivideExpression::evaluate_integer() const {
    return this->left->evaluate_integer() / this->right->evaluate_integer();
}

double DivideExpression::evaluate_number() const {
    return this->left->evaluate_number() / this->right->evaluate_number();
}

AddExpression::AddExpression(const Expression_ptr left, const Expression_ptr right)
    : Expression(get_common_number_type(left, right)), left(left), right(right) {
}

AddExpression::~AddExpression() {
}

int64_t AddExpression::evaluate_integer() const {
    return this->left->evaluate_integer() + this->right->evaluate_integer();
}

double AddExpression::evaluate_number() const {
    return this->left->evaluate_number() + this->right->evaluate_number();
}

SubtractExpression::SubtractExpression(const Expression_ptr left, const Expression_ptr right)
    : Expression(get_common_number_type(left, right)), left(left), right(right) {
}

SubtractExpression::~SubtractExpression() {
}

int64_t SubtractExpression::evaluate_integer() const {
    return this->left->evaluate_integer() - this->right->evaluate_integer();
}

double SubtractExpression::evaluate_number() const {
    return this->left->evaluate_number() - this->right->evaluate_number();
}

ShiftLeftExpression::ShiftLeftExpression(const Expression_ptr left, const Expression_ptr right)
    : Expression(integer), left(left), right(right) {
}

ShiftLeftExpression::~ShiftLeftExpression() {
}

int64_t ShiftLeftExpression::evaluate_integer() const {
    return this->left->evaluate_integer() << this->right->evaluate_integer();
}

ShiftRightExpression::ShiftRightExpression(const Expression_ptr left, const Expression_ptr right)
    : Expression(integer), left(left), right(right) {
}

ShiftRightExpression::~ShiftRightExpression() {
}

int64_t ShiftRightExpression::evaluate_integer() const {
    return this->left->evaluate_integer() >> this->right->evaluate_integer();
}

BitAndExpression::BitAndExpression(const Expression_ptr left, const Expression_ptr right)
    : Expression(integer), left(left), right(right) {
}

BitAndExpression::~BitAndExpression() {
}

int64_t BitAndExpression::evaluate_integer() const {
    return this->left->evaluate_integer() & this->right->evaluate_integer();
}

BitXorExpression::BitXorExpression(const Expression_ptr left, const Expression_ptr right)
    : Expression(integer), left(left), right(right) {
}

BitXorExpression::~BitXorExpression() {
}

int64_t BitXorExpression::evaluate_integer() const {
    return this->left->evaluate_integer() ^ this->right->evaluate_integer();
}

BitOrExpression::BitOrExpression(const Expression_ptr left, const Expression_ptr right)
    : Expression(integer), left(left), right(right) {
}

BitOrExpression::~BitOrExpression() {
}

int64_t BitOrExpression::evaluate_integer() const {
    return this->left->evaluate_integer() | this->right->evaluate_integer();
}

GreaterExpression::GreaterExpression(const Expression_ptr left, const Expression_ptr right)
    : Expression(boolean), left(left), right(right) {
    check_number_types(left, right);
}

GreaterExpression::~GreaterExpression() {
}

bool GreaterExpression::evaluate_boolean() const {
    return this->left->evaluate_number() > this->right->evaluate_number();
}

LessExpression::LessExpression(const Expression_ptr left, const Expression_ptr right)
    : Expression(boolean), left(left), right(right) {
    check_number_types(left, right);
}

LessExpression::~LessExpression() {
}

bool LessExpression::evaluate_boolean() const {
    return this->left->evaluate_number() < this->right->evaluate_number();
}

GreaterEqualExpression::GreaterEqualExpression(const Expression_ptr left, const Expression_ptr right)
    : Expression(boolean), left(left), right(right) {
    check_number_types(left, right);
}

GreaterEqualExpression::~GreaterEqualExpression() {
}

bool GreaterEqualExpression::evaluate_boolean() const {
    return this->left->evaluate_number() >= this->right->evaluate_number();
}

LessEqualExpression::LessEqualExpression(const Expression_ptr left, const Expression_ptr right)
    : Expression(boolean), left(left), right(right) {
    check_number_types(left, right);
}

LessEqualExpression::~LessEqualExpression() {
}

bool LessEqualExpression::evaluate_boolean() const {
    return this->left->evaluate_number() <= this->right->evaluate_number();
}

EqualExpression::EqualExpression(const Expression_ptr left, const Expression_ptr right)
    : Expression(boolean), left(left), right(right) {
    check_number_types(left, right);
}

EqualExpression::~EqualExpression() {
}

bool EqualExpression::evaluate_boolean() const {
    return this->left->evaluate_number() == this->right->evaluate_number();
}

UnequalExpression::UnequalExpression(const Expression_ptr left, const Expression_ptr right)
    : Expression(boolean), left(left), right(right) {
    check_number_types(left, right);
}

UnequalExpression::~UnequalExpression() {
}

bool UnequalExpression::evaluate_boolean() const {
    return this->left->evaluate_number() != this->right->evaluate_number();
}

NotExpression::NotExpression(const Expression_ptr operand)
    : Expression(boolean), operand(operand) {
    check_boolean_types(operand, operand);
}

NotExpression::~NotExpression() {
}

bool NotExpression::evaluate_boolean() const {
    return !this->operand->evaluate_boolean();
}

AndExpression::AndExpression(const Expression_ptr left, const Expression_ptr right)
    : Expression(boolean), left(left), right(right) {
    check_boolean_types(left, right);
}

AndExpression::~AndExpression() {
}

bool AndExpression::evaluate_boolean() const {
    return this->left->evaluate_boolean() && this->right->evaluate_boolean();
}

OrExpression::OrExpression(const Expression_ptr left, const Expression_ptr right)
    : Expression(boolean), left(left), right(right) {
    check_boolean_types(left, right);
}

OrExpression::~OrExpression() {
}

bool OrExpression::evaluate_boolean() const {
    return this->left->evaluate_boolean() || this->right->evaluate_boolean();
}
