#pragma once
#include "module.h"

class Expandable;
using Expandable_ptr = std::shared_ptr<Expandable>;

class Expandable {
public:
    virtual void send_proxy(const std::string module_name, const std::string module_type, const std::vector<ConstExpression_ptr> arguments) = 0;
    virtual void send_property(const std::string proxy_name, const std::string property_name, const ConstExpression_ptr expression) = 0;
    virtual void send_call(const std::string proxy_name, const std::string method_name, const std::vector<ConstExpression_ptr> arguments) = 0;
    virtual bool is_ready() const = 0;
    virtual std::string get_name() const = 0;
    virtual ~Expandable() = default;
};
