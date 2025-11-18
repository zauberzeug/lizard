#pragma once

#include "expandable.h"
#include "module.h"

class Proxy : public Module, public Expandable {
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

    // Method to get parent expander name
    std::string get_parent_expander_name() const;

    // Expandable interface implementation (for chaining)
    void send_proxy(const std::string module_name, const std::string module_type, const std::vector<ConstExpression_ptr> arguments) override;
    void send_property(const std::string proxy_name, const std::string property_name, const ConstExpression_ptr expression) override;
    void send_call(const std::string proxy_name, const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    bool is_ready() const override;
    std::string get_name() const override;
};
