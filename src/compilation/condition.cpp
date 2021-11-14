#include "condition.h"

Condition::Condition(Expression *left_expression, Expression *right_expression, bool equality)
{
    this->left_expression = left_expression;
    this->right_expression = right_expression;
    this->equality = equality;
}

bool Condition::evaluate()
{
    double left = this->left_expression->evaluate();
    double right = this->right_expression->evaluate();

    if (this->equality)
    {
        return left == right;
    }
    else
    {
        return left != right;
    }
}
