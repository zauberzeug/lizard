#pragma once

#include "expander.h"
#include "module.h"

class Proxy : public Module {
private:
    const Expander_ptr expander;

public:
    Proxy(const std::string name,
          const std::string expander_name,
          const std::string module_type,
          const Expander_ptr expander,
          const std::vector<ConstExpression_ptr> arguments);
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void write_property(const std::string property_name, const ConstExpression_ptr expression) override;
};