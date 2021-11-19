#include "condition.h"

Condition::Condition(Expression *left_expression, Expression *right_expression, Relation relation)
{
    this->left_expression = left_expression;
    this->right_expression = right_expression;
    this->relation = relation;
}

bool Condition::evaluate()
{
    double left = this->left_expression->evaluate();
    double right = this->right_expression->evaluate();

    switch (this->relation)
    {
    case equal:
        return left == right;
    case unequal:
        return left != right;
    case greater:
        return left > right;
    case less:
        return left < right;
    case greater_equal:
        return left >= right;
    case less_equal:
        return left <= right;
    }

    printf("error: invalid relation\n");
    return false;
}
