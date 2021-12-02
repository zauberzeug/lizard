#pragma once

#include <string>
#include <vector>
#include "../modules/module.h"
#include "action.h"
#include "expression.h"

class MethodCall : public Action
{
public:
    Module *const module;
    const std::string method_name;
    const std::vector<const Expression *> arguments;

    MethodCall(Module *const module, const std::string method_name, const std::vector<const Expression *> arguments);
    ~MethodCall();
    bool run();
};