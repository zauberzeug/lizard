#pragma once

#include "external_expander.h"
#include "module.h"

class ExternalProxy : public Module {
private:
    const ExternalExpander_ptr external_expander;

public:
    ExternalProxy(const std::string name,
                  const std::string expander_name,
                  const std::string module_type,
                  const ExternalExpander_ptr external_expander,
                  const std::vector<ConstExpression_ptr> arguments);
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) override;
};