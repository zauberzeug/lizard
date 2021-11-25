#pragma once

#include <string>
#include <vector>
#include "../compilation/expression.h"
#include "module.h"

class Proxy : public Module
{
public:
    Proxy(std::string name, std::vector<Expression *> arguments);
    void call(std::string method_name, std::vector<Expression *> arguments);
};