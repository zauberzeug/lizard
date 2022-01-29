#pragma once

#include "action.h"
#include "expression.h"

class AwaitCondition : public Action {
public:
    const ConstExpression_ptr condition;

    AwaitCondition(const ConstExpression_ptr condition);
    bool run() override;
};