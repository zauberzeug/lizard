#pragma once

#include "action.h"
#include "expression.h"

class AwaitCondition : public Action {
public:
    const Expression *const condition;

    AwaitCondition(const Expression *const condition);
    ~AwaitCondition();
    bool run();
};