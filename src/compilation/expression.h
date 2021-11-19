#pragma once

#include <string>
#include "type.h"

class Expression
{
public:
    Type type;
    virtual bool evaluate_boolean();
    virtual int evaluate_integer();
    virtual double evaluate_number();
    virtual std::string evaluate_identifier();
    virtual std::string evaluate_string();
    bool is_numbery();
};

class IntegerExpression : public Expression
{
private:
    int value;

public:
    IntegerExpression(int value);
    int evaluate_integer();
};

class StringExpression : public Expression
{
private:
    std::string value;

public:
    StringExpression(std::string value);
    std::string evaluate_string();
};
