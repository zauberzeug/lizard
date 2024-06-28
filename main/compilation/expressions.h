#pragma once

#include "../modules/module.h"
#include "expression.h"

class BooleanExpression : public Expression {
private:
    const bool value;

public:
    BooleanExpression(const bool value);
    bool evaluate_boolean() const override;
};

class StringExpression : public Expression {
private:
    const std::string value;

public:
    StringExpression(const std::string value);
    std::string evaluate_string() const override;
};

class IntegerExpression : public Expression {
private:
    const int64_t value;

public:
    IntegerExpression(const int64_t value);
    int64_t evaluate_integer() const override;
    double evaluate_number() const override;
};

class NumberExpression : public Expression {
private:
    const double value;

public:
    NumberExpression(const double value);
    double evaluate_number() const override;
};

class VariableExpression : public Expression {
private:
    const ConstVariable_ptr variable;

public:
    VariableExpression(const ConstVariable_ptr variable);
    bool evaluate_boolean() const override;
    int64_t evaluate_integer() const override;
    double evaluate_number() const override;
    std::string evaluate_string() const override;
    std::string evaluate_identifier() const override;
};

class PropertyExpression : public Expression {
private:
    const ConstModule_ptr module;
    const std::string property_name;

public:
    PropertyExpression(const ConstModule_ptr module, const std::string property_name);
    bool evaluate_boolean() const override;
    int64_t evaluate_integer() const override;
    double evaluate_number() const override;
    std::string evaluate_string() const override;
    std::string evaluate_identifier() const override;
};

class PowerExpression : public Expression {
private:
    const ConstExpression_ptr left;
    const ConstExpression_ptr right;

public:
    PowerExpression(const ConstExpression_ptr left, const ConstExpression_ptr right);
    int64_t evaluate_integer() const override;
    double evaluate_number() const override;
};

class NegateExpression : public Expression {
private:
    const ConstExpression_ptr operand;

public:
    NegateExpression(const ConstExpression_ptr operand);
    int64_t evaluate_integer() const override;
    double evaluate_number() const override;
};

class MultiplyExpression : public Expression {
private:
    const ConstExpression_ptr left;
    const ConstExpression_ptr right;

public:
    MultiplyExpression(const ConstExpression_ptr left, const ConstExpression_ptr right);
    int64_t evaluate_integer() const override;
    double evaluate_number() const override;
};

class DivideExpression : public Expression {
private:
    const ConstExpression_ptr left;
    const ConstExpression_ptr right;

public:
    DivideExpression(const ConstExpression_ptr left, const ConstExpression_ptr right);
    int64_t evaluate_integer() const override;
    double evaluate_number() const override;
};

class ModuloExpression : public Expression {
private:
    const ConstExpression_ptr left;
    const ConstExpression_ptr right;

public:
    ModuloExpression(const ConstExpression_ptr left, const ConstExpression_ptr right);
    int64_t evaluate_integer() const override;
    double evaluate_number() const override;
};

class FloorDivideExpression : public Expression {
private:
    const ConstExpression_ptr left;
    const ConstExpression_ptr right;

public:
    FloorDivideExpression(const ConstExpression_ptr left, const ConstExpression_ptr right);
    int64_t evaluate_integer() const override;
    double evaluate_number() const override;
};

class AddExpression : public Expression {
private:
    const ConstExpression_ptr left;
    const ConstExpression_ptr right;

public:
    AddExpression(const ConstExpression_ptr left, const ConstExpression_ptr right);
    int64_t evaluate_integer() const override;
    double evaluate_number() const override;
};

class SubtractExpression : public Expression {
private:
    const ConstExpression_ptr left;
    const ConstExpression_ptr right;

public:
    SubtractExpression(const ConstExpression_ptr left, const ConstExpression_ptr right);
    int64_t evaluate_integer() const override;
    double evaluate_number() const override;
};

class ShiftLeftExpression : public Expression {
private:
    const ConstExpression_ptr left;
    const ConstExpression_ptr right;

public:
    ShiftLeftExpression(const ConstExpression_ptr left, const ConstExpression_ptr right);
    int64_t evaluate_integer() const override;
};

class ShiftRightExpression : public Expression {
private:
    const ConstExpression_ptr left;
    const ConstExpression_ptr right;

public:
    ShiftRightExpression(const ConstExpression_ptr left, const ConstExpression_ptr right);
    int64_t evaluate_integer() const override;
};

class BitAndExpression : public Expression {
private:
    const ConstExpression_ptr left;
    const ConstExpression_ptr right;

public:
    BitAndExpression(const ConstExpression_ptr left, const ConstExpression_ptr right);
    int64_t evaluate_integer() const override;
};

class BitXorExpression : public Expression {
private:
    const ConstExpression_ptr left;
    const ConstExpression_ptr right;

public:
    BitXorExpression(const ConstExpression_ptr left, const ConstExpression_ptr right);
    int64_t evaluate_integer() const override;
};

class BitOrExpression : public Expression {
private:
    const ConstExpression_ptr left;
    const ConstExpression_ptr right;

public:
    BitOrExpression(const ConstExpression_ptr left, const ConstExpression_ptr right);
    int64_t evaluate_integer() const override;
};

class GreaterExpression : public Expression {
private:
    const ConstExpression_ptr left;
    const ConstExpression_ptr right;

public:
    GreaterExpression(const ConstExpression_ptr left, const ConstExpression_ptr right);
    bool evaluate_boolean() const override;
};

class LessExpression : public Expression {
private:
    const ConstExpression_ptr left;
    const ConstExpression_ptr right;

public:
    LessExpression(const ConstExpression_ptr left, const ConstExpression_ptr right);
    bool evaluate_boolean() const override;
};

class GreaterEqualExpression : public Expression {
private:
    const ConstExpression_ptr left;
    const ConstExpression_ptr right;

public:
    GreaterEqualExpression(const ConstExpression_ptr left, const ConstExpression_ptr right);
    bool evaluate_boolean() const override;
};

class LessEqualExpression : public Expression {
private:
    const ConstExpression_ptr left;
    const ConstExpression_ptr right;

public:
    LessEqualExpression(const ConstExpression_ptr left, const ConstExpression_ptr right);
    bool evaluate_boolean() const override;
};

class EqualExpression : public Expression {
private:
    const ConstExpression_ptr left;
    const ConstExpression_ptr right;

public:
    EqualExpression(const ConstExpression_ptr left, const ConstExpression_ptr right);
    bool evaluate_boolean() const override;
};

class UnequalExpression : public Expression {
private:
    const ConstExpression_ptr left;
    const ConstExpression_ptr right;

public:
    UnequalExpression(const ConstExpression_ptr left, const ConstExpression_ptr right);
    bool evaluate_boolean() const override;
};

class NotExpression : public Expression {
private:
    const ConstExpression_ptr operand;

public:
    NotExpression(const ConstExpression_ptr operand);
    bool evaluate_boolean() const override;
};

class AndExpression : public Expression {
private:
    const ConstExpression_ptr left;
    const ConstExpression_ptr right;

public:
    AndExpression(const ConstExpression_ptr left, const ConstExpression_ptr right);
    bool evaluate_boolean() const override;
};

class OrExpression : public Expression {
private:
    const ConstExpression_ptr left;
    const ConstExpression_ptr right;

public:
    OrExpression(const ConstExpression_ptr left, const ConstExpression_ptr right);
    bool evaluate_boolean() const override;
};
