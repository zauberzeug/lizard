#include "await_condition.h"

AwaitCondition::AwaitCondition(Expression *condition)
{
    this->condition = condition;
}

bool AwaitCondition::run()
{
    return this->condition->evaluate_boolean();
}