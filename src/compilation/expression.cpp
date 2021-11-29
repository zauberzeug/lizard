#include "expression.h"

#include <stdexcept>
#include "math.h"

Type get_common_number_type(Expression *left, Expression *right)
{
    if (left->type == integer && right->type == integer)
    {
        return integer;
    }
    else if (left->is_numbery() && right->is_numbery())
    {
        return number;
    }
    else
    {
        throw std::runtime_error("invalid type for arithmetic operation");
    }
}

void check_number_types(Expression *left, Expression *right)
{
    if (!left->is_numbery() || !right->is_numbery())
    {
        throw std::runtime_error("invalid type for comparison");
    }
}

void check_boolean_types(Expression *left, Expression *right)
{
    if (left->type != boolean || right->type != boolean)
    {
        throw std::runtime_error("invalid type for logical operation");
    }
}

Expression::~Expression()
{
}

bool Expression::evaluate_boolean()
{
    throw std::runtime_error("not implemented");
}

int64_t Expression::evaluate_integer()
{
    throw std::runtime_error("not implemented");
}

double Expression::evaluate_number()
{
    throw std::runtime_error("not implemented");
}

std::string Expression::evaluate_identifier()
{
    throw std::runtime_error("not implemented");
}

std::string Expression::evaluate_string()
{
    throw std::runtime_error("not implemented");
}

bool Expression::is_numbery()
{
    return this->type == number || this->type == integer;
}

int Expression::print_to_buffer(char *buffer)
{
    switch (this->type)
    {
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
{
    this->type = boolean;
    this->value = value;
}

bool BooleanExpression::evaluate_boolean()
{
    return this->value;
}

StringExpression::StringExpression(std::string value)
{
    this->type = string;
    this->value = value;
}

std::string StringExpression::evaluate_string()
{
    return this->value;
}

IntegerExpression::IntegerExpression(int64_t value)
{
    this->type = integer;
    this->value = value;
}

int64_t IntegerExpression::evaluate_integer()
{
    return this->value;
}

double IntegerExpression::evaluate_number()
{
    return this->value;
}

NumberExpression::NumberExpression(double value)
{
    this->type = number;
    this->value = value;
}

double NumberExpression::evaluate_number()
{
    return this->value;
}

VariableExpression::VariableExpression(Variable *variable)
{
    this->type = variable->type;
    this->variable = variable;
}

bool VariableExpression::evaluate_boolean()
{
    if (this->type != boolean)
    {
        throw std::runtime_error("variable is not a boolean");
    }
    return this->variable->boolean_value;
}

int64_t VariableExpression::evaluate_integer()
{
    if (this->type != integer)
    {
        throw std::runtime_error("variable is not an integer");
    }
    return this->variable->integer_value;
}

double VariableExpression::evaluate_number()
{
    if (!this->is_numbery())
    {
        throw std::runtime_error("variable is not a number");
    }
    return this->type == number ? this->variable->number_value : this->variable->integer_value;
}

std::string VariableExpression::evaluate_string()
{
    if (this->type != string)
    {
        throw std::runtime_error("variable is not a string");
    }
    return this->variable->string_value;
}

std::string VariableExpression::evaluate_identifier()
{
    if (this->type != identifier)
    {
        throw std::runtime_error("variable is not an identifier");
    }
    return this->variable->identifier_value;
}

PowerExpression::PowerExpression(Expression *left, Expression *right)
{
    this->type = get_common_number_type(left, right);
    this->left = left;
    this->right = right;
}

PowerExpression::~PowerExpression()
{
    delete this->left;
    delete this->right;
}

int64_t PowerExpression::evaluate_integer()
{
    return pow(this->left->evaluate_integer(), this->right->evaluate_integer());
}

double PowerExpression::evaluate_number()
{
    return pow(this->left->evaluate_number(), this->right->evaluate_number());
}

NegateExpression::NegateExpression(Expression *operand)
{
    this->type = get_common_number_type(operand, operand);
    this->operand = operand;
}

NegateExpression::~NegateExpression()
{
    delete this->operand;
}

int64_t NegateExpression::evaluate_integer()
{
    return -this->operand->evaluate_integer();
}

double NegateExpression::evaluate_number()
{
    return -this->operand->evaluate_number();
}

MultiplyExpression::MultiplyExpression(Expression *left, Expression *right)
{
    this->type = get_common_number_type(left, right);
    this->left = left;
    this->right = right;
}

MultiplyExpression::~MultiplyExpression()
{
    delete this->left;
    delete this->right;
}

int64_t MultiplyExpression::evaluate_integer()
{
    return this->left->evaluate_integer() * this->right->evaluate_integer();
}

double MultiplyExpression::evaluate_number()
{
    return this->left->evaluate_number() * this->right->evaluate_number();
}

DivideExpression::DivideExpression(Expression *left, Expression *right)
{
    this->type = get_common_number_type(left, right);
    this->left = left;
    this->right = right;
}

DivideExpression::~DivideExpression()
{
    delete this->left;
    delete this->right;
}

int64_t DivideExpression::evaluate_integer()
{
    return this->left->evaluate_integer() / this->right->evaluate_integer();
}

double DivideExpression::evaluate_number()
{
    return this->left->evaluate_number() / this->right->evaluate_number();
}

AddExpression::AddExpression(Expression *left, Expression *right)
{
    this->type = get_common_number_type(left, right);
    this->left = left;
    this->right = right;
}

AddExpression::~AddExpression()
{
    delete this->left;
    delete this->right;
}

int64_t AddExpression::evaluate_integer()
{
    return this->left->evaluate_integer() + this->right->evaluate_integer();
}

double AddExpression::evaluate_number()
{
    return this->left->evaluate_number() + this->right->evaluate_number();
}

SubtractExpression::SubtractExpression(Expression *left, Expression *right)
{
    this->type = get_common_number_type(left, right);
    this->left = left;
    this->right = right;
}

SubtractExpression::~SubtractExpression()
{
    delete this->left;
    delete this->right;
}

int64_t SubtractExpression::evaluate_integer()
{
    return this->left->evaluate_integer() - this->right->evaluate_integer();
}

double SubtractExpression::evaluate_number()
{
    return this->left->evaluate_number() - this->right->evaluate_number();
}

ShiftLeftExpression::ShiftLeftExpression(Expression *left, Expression *right)
{
    this->type = integer;
    this->left = left;
    this->right = right;
}

ShiftLeftExpression::~ShiftLeftExpression()
{
    delete this->left;
    delete this->right;
}

int64_t ShiftLeftExpression::evaluate_integer()
{
    return this->left->evaluate_integer() << this->right->evaluate_integer();
}

ShiftRightExpression::ShiftRightExpression(Expression *left, Expression *right)
{
    this->type = integer;
    this->left = left;
    this->right = right;
}

ShiftRightExpression::~ShiftRightExpression()
{
    delete this->left;
    delete this->right;
}

int64_t ShiftRightExpression::evaluate_integer()
{
    return this->left->evaluate_integer() >> this->right->evaluate_integer();
}

BitAndExpression::BitAndExpression(Expression *left, Expression *right)
{
    this->type = integer;
    this->left = left;
    this->right = right;
}

BitAndExpression::~BitAndExpression()
{
    delete this->left;
    delete this->right;
}

int64_t BitAndExpression::evaluate_integer()
{
    return this->left->evaluate_integer() & this->right->evaluate_integer();
}

BitXorExpression::BitXorExpression(Expression *left, Expression *right)
{
    this->type = integer;
    this->left = left;
    this->right = right;
}

BitXorExpression::~BitXorExpression()
{
    delete this->left;
    delete this->right;
}

int64_t BitXorExpression::evaluate_integer()
{
    return this->left->evaluate_integer() ^ this->right->evaluate_integer();
}

BitOrExpression::BitOrExpression(Expression *left, Expression *right)
{
    this->type = integer;
    this->left = left;
    this->right = right;
}

BitOrExpression::~BitOrExpression()
{
    delete this->left;
    delete this->right;
}

int64_t BitOrExpression::evaluate_integer()
{
    return this->left->evaluate_integer() | this->right->evaluate_integer();
}

GreaterExpression::GreaterExpression(Expression *left, Expression *right)
{
    check_number_types(left, right);
    this->type = boolean;
    this->left = left;
    this->right = right;
}

GreaterExpression::~GreaterExpression()
{
    delete this->left;
    delete this->right;
}

bool GreaterExpression::evaluate_boolean()
{
    return this->left->evaluate_number() > this->right->evaluate_number();
}

LessExpression::LessExpression(Expression *left, Expression *right)
{
    check_number_types(left, right);
    this->type = boolean;
    this->left = left;
    this->right = right;
}

LessExpression::~LessExpression()
{
    delete this->left;
    delete this->right;
}

bool LessExpression::evaluate_boolean()
{
    return this->left->evaluate_number() < this->right->evaluate_number();
}

GreaterEqualExpression::GreaterEqualExpression(Expression *left, Expression *right)
{
    check_number_types(left, right);
    this->type = boolean;
    this->left = left;
    this->right = right;
}

GreaterEqualExpression::~GreaterEqualExpression()
{
    delete this->left;
    delete this->right;
}

bool GreaterEqualExpression::evaluate_boolean()
{
    return this->left->evaluate_number() >= this->right->evaluate_number();
}

LessEqualExpression::LessEqualExpression(Expression *left, Expression *right)
{
    check_number_types(left, right);
    this->type = boolean;
    this->left = left;
    this->right = right;
}

LessEqualExpression::~LessEqualExpression()
{
    delete this->left;
    delete this->right;
}

bool LessEqualExpression::evaluate_boolean()
{
    return this->left->evaluate_number() <= this->right->evaluate_number();
}

EqualExpression::EqualExpression(Expression *left, Expression *right)
{
    check_number_types(left, right);
    this->type = boolean;
    this->left = left;
    this->right = right;
}

EqualExpression::~EqualExpression()
{
    delete this->left;
    delete this->right;
}

bool EqualExpression::evaluate_boolean()
{
    return this->left->evaluate_number() == this->right->evaluate_number();
}

UnequalExpression::UnequalExpression(Expression *left, Expression *right)
{
    check_number_types(left, right);
    this->type = boolean;
    this->left = left;
    this->right = right;
}

UnequalExpression::~UnequalExpression()
{
    delete this->left;
    delete this->right;
}

bool UnequalExpression::evaluate_boolean()
{
    return this->left->evaluate_number() != this->right->evaluate_number();
}

NotExpression::NotExpression(Expression *operand)
{
    check_boolean_types(operand, operand);
    this->type = boolean;
    this->operand = operand;
}

NotExpression::~NotExpression()
{
    delete this->operand;
}

bool NotExpression::evaluate_boolean()
{
    return !this->operand->evaluate_boolean();
}

AndExpression::AndExpression(Expression *left, Expression *right)
{
    check_boolean_types(left, right);
    this->type = boolean;
    this->left = left;
    this->right = right;
}

AndExpression::~AndExpression()
{
    delete this->left;
    delete this->right;
}

bool AndExpression::evaluate_boolean()
{
    return this->left->evaluate_boolean() && this->right->evaluate_boolean();
}

OrExpression::OrExpression(Expression *left, Expression *right)
{
    check_boolean_types(left, right);
    this->type = boolean;
    this->left = left;
    this->right = right;
}

OrExpression::~OrExpression()
{
    delete this->left;
    delete this->right;
}

bool OrExpression::evaluate_boolean()
{
    return this->left->evaluate_boolean() || this->right->evaluate_boolean();
}
