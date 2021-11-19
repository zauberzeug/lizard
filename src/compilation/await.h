#pragma once

#include "action.h"
#include "expression.h"

class Await : public Action
{
public:
    Expression *condition;

    Await(Expression *condition);
    bool run();
};