#pragma once

#include "expandable.h"
#include "module.h"

class Proxy : public Module {
private:
    const Expandable_ptr expandable;

public:
    Proxy(const std::string name,
          const std::string expander_name,
          const std::string module_type,
          const Expandable_ptr expandable,
          const std::vector<ConstExpression_ptr> arguments);
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) override;
};
