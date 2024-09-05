#pragma once

#include "expander.h"
#include "external_expander.h"
#include "module.h"

class Proxy : public Module {
private:
    const Expander_ptr expander;
    const ExternalExpander_ptr external_expander;

public:
    Proxy(const std::string name,
          const std::string expander_name,
          const std::string module_type,
          const Expander_ptr expander,
          const std::vector<ConstExpression_ptr> arguments);

    Proxy(const std::string &name,
          const std::string &expander_name,
          const std::string &module_type,
          std::shared_ptr<ExternalExpander> external_expander,
          const std::vector<ConstExpression_ptr> &arguments);
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) override;
};