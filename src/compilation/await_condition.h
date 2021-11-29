#pragma once

#include "action.h"
#include "expression.h"

class AwaitCondition : public Action
{
public:
    Expression *condition;

    AwaitCondition(Expression *condition);
    ~AwaitCondition();
    bool run();
};