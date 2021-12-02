#pragma once

#include "module.h"

class Proxy : public Module
{
public:
    Proxy(const std::string name);
    void call(const std::string method_name, const std::vector<const Expression *> arguments);
    void write_property(const std::string property_name, const Expression *const expression);
};