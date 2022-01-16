#pragma once

#include "../modules/module.h"
#include "action.h"
#include "expression.h"
#include <string>
#include <vector>

class MethodCall : public Action {
public:
    Module *const module;
    const std::string method_name;
    const std::vector<Expression_ptr> arguments;

    MethodCall(Module *const module, const std::string method_name, const std::vector<Expression_ptr> arguments);
    ~MethodCall();
    bool run();
};