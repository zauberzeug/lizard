#pragma once

#include "expression.h"

enum Relation
{
    less = 1,
    equal,
    greater,
    unequal,
    less_equal,
    greater_equal,
};

class Condition
{
private:
    Expression *left_expression;
    Expression *right_expression;
    Relation relation;

public:
    Condition(Expression *left_expression, Expression *right_expression, Relation relation);
    bool evaluate();
};