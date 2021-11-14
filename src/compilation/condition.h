#pragma once

#include "expression.h"

class Condition
{
private:
    Expression *left_expression;
    Expression *right_expression;
    bool equality;

public:
    Condition(Expression *left_expression, Expression *right_expression, bool equality);
    bool evaluate();
};