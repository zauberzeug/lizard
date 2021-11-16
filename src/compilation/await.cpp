#include "await.h"

Await::Await(Condition *condition)
{
    this->condition = condition;
}

bool Await::run()
{
    return this->condition->evaluate();
}