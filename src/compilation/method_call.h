#pragma once

#include <string>
#include <vector>
#include "../modules/module.h"
#include "action.h"
#include "argument.h"

class MethodCall : public Action
{
public:
    Module *module;
    std::string method_name;
    std::vector<Argument *> arguments;

    MethodCall(Module *module, std::string method_name, std::vector<Argument *> arguments);
    bool run();
};