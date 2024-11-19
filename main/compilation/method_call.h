#pragma once

#include "../modules/module.h"
#include "action.h"
#include "expression.h"
#include <string>
#include <vector>

class MethodCall : public Action {
public:
    const Module_ptr module;
    const std::string method_name;
    const std::vector<ConstExpression_ptr> arguments;

    MethodCall(const Module_ptr module, const std::string method_name, const std::vector<ConstExpression_ptr> arguments);
    bool run() override;
};
