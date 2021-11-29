#pragma once

#include <string>
#include <vector>
#include "../modules/module.h"
#include "action.h"
#include "expression.h"

class MethodCall : public Action
{
public:
    Module *module;
    std::string method_name;
    std::vector<Expression *> arguments;

    MethodCall(Module *module, std::string method_name, std::vector<Expression *> arguments);
    ~MethodCall();
    bool run();
};