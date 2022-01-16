#pragma once

#include "module.h"

class Proxy : public Module {
public:
    Proxy(const std::string name);
    void call(const std::string method_name, const std::vector<Expression_ptr> arguments);
    void write_property(const std::string property_name, const Expression_ptr expression);
};