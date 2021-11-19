#include "expression.h"

#include "math.h"
#include "../modules/module.h"

bool Expression::evaluate_boolean()
{
    return false; // TODO
}

int Expression::evaluate_integer()
{
    return 0; // TODO
}

double Expression::evaluate_number()
{
    return 0; // TODO
}

std::string Expression::evaluate_identifier()
{
    return ""; // TODO
}

std::string Expression::evaluate_string()
{
    return ""; // TODO
}

bool Expression::is_numbery()
{
    return this->type == number || this->type == integer;
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

IntegerExpression::IntegerExpression(int value)
{
    this->type = integer;
    this->value = value;
}

int IntegerExpression::evaluate_integer()
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

bool VariableExpression::evaluate_bool()
{
    return this->variable->boolean_value;
}

int VariableExpression::evaluate_integer()
{
    return this->variable->integer_value;
}

double VariableExpression::evaluate_number()
{
    return this->variable->number_value;
}

std::string VariableExpression::evaluate_string()
{
    return this->variable->string_value;
}

PropertyExpression::PropertyExpression(Module *module, std::string property_name)
{
    this->type = number; // TODO: more flexible property types
    this->module = module;
    this->property_name = property_name;
}

double PropertyExpression::evaluate_number()
{
    return this->module->get(this->property_name);
}

PowerExpression::PowerExpression(Expression *left, Expression *right)
{
    this->type = number;
    this->left = left;
    this->right = right;
}

double PowerExpression::evaluate_number()
{
    return pow(this->left->evaluate_number(), this->right->evaluate_number());
}

NegateExpression::NegateExpression(Expression *operand)
{
    this->type = number;
    this->operand = operand;
}

double NegateExpression::evaluate_number()
{
    return -this->operand->evaluate_number();
}

MultiplyExpression::MultiplyExpression(Expression *left, Expression *right)
{
    this->type = number;
    this->left = left;
    this->right = right;
}

double MultiplyExpression::evaluate_number()
{
    return this->left->evaluate_number() * this->right->evaluate_number();
}

DivideExpression::DivideExpression(Expression *left, Expression *right)
{
    this->type = number;
    this->left = left;
    this->right = right;
}

double DivideExpression::evaluate_number()
{
    return this->left->evaluate_number() / this->right->evaluate_number();
}

AddExpression::AddExpression(Expression *left, Expression *right)
{
    this->type = number;
    this->left = left;
    this->right = right;
}

double AddExpression::evaluate_number()
{
    return this->left->evaluate_number() + this->right->evaluate_number();
}

SubtractExpression::SubtractExpression(Expression *left, Expression *right)
{
    this->type = number;
    this->left = left;
    this->right = right;
}

double SubtractExpression::evaluate_number()
{
    return this->left->evaluate_number() - this->right->evaluate_number();
}

GreaterExpression::GreaterExpression(Expression *left, Expression *right)
{
    this->type = boolean;
    this->left = left;
    this->right = right;
}

bool GreaterExpression::evaluate_boolean()
{
    return this->left->evaluate_number() > this->right->evaluate_number();
}

LessExpression::LessExpression(Expression *left, Expression *right)
{
    this->type = boolean;
    this->left = left;
    this->right = right;
}

bool LessExpression::evaluate_boolean()
{
    return this->left->evaluate_number() < this->right->evaluate_number();
}

GreaterEqualExpression::GreaterEqualExpression(Expression *left, Expression *right)
{
    this->type = boolean;
    this->left = left;
    this->right = right;
}

bool GreaterEqualExpression::evaluate_boolean()
{
    return this->left->evaluate_number() >= this->right->evaluate_number();
}

LessEqualExpression::LessEqualExpression(Expression *left, Expression *right)
{
    this->type = boolean;
    this->left = left;
    this->right = right;
}

bool LessEqualExpression::evaluate_boolean()
{
    return this->left->evaluate_number() <= this->right->evaluate_number();
}

EqualExpression::EqualExpression(Expression *left, Expression *right)
{
    this->type = boolean;
    this->left = left;
    this->right = right;
}

bool EqualExpression::evaluate_boolean()
{
    return this->left->evaluate_number() == this->right->evaluate_number();
}

UnequalExpression::UnequalExpression(Expression *left, Expression *right)
{
    this->type = boolean;
    this->left = left;
    this->right = right;
}

bool UnequalExpression::evaluate_boolean()
{
    return this->left->evaluate_number() != this->right->evaluate_number();
}

NotExpression::NotExpression(Expression *operand)
{
    this->type = boolean;
    this->operand = operand;
}

bool NotExpression::evaluate_boolean()
{
    return !this->operand->evaluate_boolean();
}

AndExpression::AndExpression(Expression *left, Expression *right)
{
    this->type = boolean;
    this->left = left;
    this->right = right;
}

bool AndExpression::evaluate_boolean()
{
    return this->left->evaluate_boolean() && this->right->evaluate_boolean();
}

OrExpression::OrExpression(Expression *left, Expression *right)
{
    this->type = boolean;
    this->left = left;
    this->right = right;
}

bool OrExpression::evaluate_boolean()
{
    return this->left->evaluate_boolean() || this->right->evaluate_boolean();
}
