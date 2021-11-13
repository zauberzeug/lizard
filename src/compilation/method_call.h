#pragma once

#include <string>
#include "../modules/module.h"
#include "action.h"

class MethodCall : public Action
{
public:
    Module *module;
    std::string method_name;

    MethodCall(Module *module, std::string method_name);
    void run();
};