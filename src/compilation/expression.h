#pragma once

#include <string>
#include "type.h"
#include "variable.h"

class Expression
{
protected:
    Expression(const Type type);

public:
    const Type type;

    virtual ~Expression();
    virtual bool evaluate_boolean() const;
    virtual int64_t evaluate_integer() const;
    virtual double evaluate_number() const;
    virtual std::string evaluate_string() const;
    virtual std::string evaluate_identifier() const;
    bool is_numbery() const;
    int print_to_buffer(char *buffer) const;
};

class BooleanExpression : public Expression
{
private:
    const bool value;

public:
    BooleanExpression(const bool value);
    bool evaluate_boolean() const;
};

class StringExpression : public Expression
{
private:
    const std::string value;

public:
    StringExpression(const std::string value);
    std::string evaluate_string() const;
};

class IntegerExpression : public Expression
{
private:
    const int64_t value;

public:
    IntegerExpression(const int64_t value);
    int64_t evaluate_integer() const;
    double evaluate_number() const;
};

class NumberExpression : public Expression
{
private:
    const double value;

public:
    NumberExpression(const double value);
    double evaluate_number() const;
};

class VariableExpression : public Expression
{
private:
    const Variable *const variable;

public:
    VariableExpression(const Variable *const variable);
    bool evaluate_boolean() const;
    int64_t evaluate_integer() const;
    double evaluate_number() const;
    std::string evaluate_string() const;
    std::string evaluate_identifier() const;
};

class PowerExpression : public Expression
{
private:
    const Expression *const left;
    const Expression *const right;

public:
    PowerExpression(const Expression *const left, const Expression *const right);
    ~PowerExpression();
    int64_t evaluate_integer() const;
    double evaluate_number() const;
};

class NegateExpression : public Expression
{
private:
    const Expression *const operand;

public:
    NegateExpression(const Expression *const operand);
    ~NegateExpression();
    int64_t evaluate_integer() const;
    double evaluate_number() const;
};

class MultiplyExpression : public Expression
{
private:
    const Expression *const left;
    const Expression *const right;

public:
    MultiplyExpression(const Expression *const left, const Expression *const right);
    ~MultiplyExpression();
    int64_t evaluate_integer() const;
    double evaluate_number() const;
};

class DivideExpression : public Expression
{
private:
    const Expression *const left;
    const Expression *const right;

public:
    DivideExpression(const Expression *const left, const Expression *const right);
    ~DivideExpression();
    int64_t evaluate_integer() const;
    double evaluate_number() const;
};

class AddExpression : public Expression
{
private:
    const Expression *const left;
    const Expression *const right;

public:
    AddExpression(const Expression *const left, const Expression *const right);
    ~AddExpression();
    int64_t evaluate_integer() const;
    double evaluate_number() const;
};

class SubtractExpression : public Expression
{
private:
    const Expression *const left;
    const Expression *const right;

public:
    SubtractExpression(const Expression *const left, const Expression *const right);
    ~SubtractExpression();
    int64_t evaluate_integer() const;
    double evaluate_number() const;
};

class ShiftLeftExpression : public Expression
{
private:
    const Expression *const left;
    const Expression *const right;

public:
    ShiftLeftExpression(const Expression *const left, const Expression *const right);
    ~ShiftLeftExpression();
    int64_t evaluate_integer() const;
};

class ShiftRightExpression : public Expression
{
private:
    const Expression *const left;
    const Expression *const right;

public:
    ShiftRightExpression(const Expression *const left, const Expression *const right);
    ~ShiftRightExpression();
    int64_t evaluate_integer() const;
};

class BitAndExpression : public Expression
{
private:
    const Expression *const left;
    const Expression *const right;

public:
    BitAndExpression(const Expression *const left, const Expression *const right);
    ~BitAndExpression();
    int64_t evaluate_integer() const;
};

class BitXorExpression : public Expression
{
private:
    const Expression *const left;
    const Expression *const right;

public:
    BitXorExpression(const Expression *const left, const Expression *const right);
    ~BitXorExpression();
    int64_t evaluate_integer() const;
};

class BitOrExpression : public Expression
{
private:
    const Expression *const left;
    const Expression *const right;

public:
    BitOrExpression(const Expression *const left, const Expression *const right);
    ~BitOrExpression();
    int64_t evaluate_integer() const;
};

class GreaterExpression : public Expression
{
private:
    const Expression *const left;
    const Expression *const right;

public:
    GreaterExpression(const Expression *const left, const Expression *const right);
    ~GreaterExpression();
    bool evaluate_boolean() const;
};

class LessExpression : public Expression
{
private:
    const Expression *const left;
    const Expression *const right;

public:
    LessExpression(const Expression *const left, const Expression *const right);
    ~LessExpression();
    bool evaluate_boolean() const;
};

class GreaterEqualExpression : public Expression
{
private:
    const Expression *const left;
    const Expression *const right;

public:
    GreaterEqualExpression(const Expression *const left, const Expression *const right);
    ~GreaterEqualExpression();
    bool evaluate_boolean() const;
};

class LessEqualExpression : public Expression
{
private:
    const Expression *const left;
    const Expression *const right;

public:
    LessEqualExpression(const Expression *const left, const Expression *const right);
    ~LessEqualExpression();
    bool evaluate_boolean() const;
};

class EqualExpression : public Expression
{
private:
    const Expression *const left;
    const Expression *const right;

public:
    EqualExpression(const Expression *const left, const Expression *const right);
    ~EqualExpression();
    bool evaluate_boolean() const;
};

class UnequalExpression : public Expression
{
private:
    const Expression *const left;
    const Expression *const right;

public:
    UnequalExpression(const Expression *const left, const Expression *const right);
    ~UnequalExpression();
    bool evaluate_boolean() const;
};

class NotExpression : public Expression
{
private:
    const Expression *const operand;

public:
    NotExpression(const Expression *const operand);
    ~NotExpression();
    bool evaluate_boolean() const;
};

class AndExpression : public Expression
{
private:
    const Expression *const left;
    const Expression *const right;

public:
    AndExpression(const Expression *const left, const Expression *const right);
    ~AndExpression();
    bool evaluate_boolean() const;
};

class OrExpression : public Expression
{
private:
    const Expression *const left;
    const Expression *const right;

public:
    OrExpression(const Expression *const left, const Expression *const right);
    ~OrExpression();
    bool evaluate_boolean() const;
};
