#pragma once

#include "action.h"
#include "expression.h"

class AwaitCondition : public Action {
public:
    const Expression_ptr condition;

    AwaitCondition(const Expression_ptr condition);
    bool run();
};