#pragma once

#include "module.h"

class Proxy : public Module
{
public:
    Proxy(std::string name, std::vector<Expression *> arguments);
    void call(std::string method_name, std::vector<Expression *> arguments);
    void write_property(std::string property_name, Expression *expression);
};