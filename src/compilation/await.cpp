#include "await.h"

Await::Await(Expression *condition)
{
    this->condition = condition;
}

bool Await::run()
{
    return this->condition->evaluate_boolean();
}