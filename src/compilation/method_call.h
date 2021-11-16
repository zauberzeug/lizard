#pragma once

#include <string>
#include <vector>
#include "../modules/module.h"
#include "action.h"

class MethodCall : public Action
{
public:
    Module *module;
    std::string method_name;
    std::vector<double> arguments;

    MethodCall(Module *module, std::string method_name, std::vector<double> arguments);
    bool run();
};