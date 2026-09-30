#include "proxy.h"
#include "../compilation/expressions.h"
#include "../utils/string_utils.h"
#include "../utils/uart.h"
#include "driver/uart.h"
#include <memory>

Proxy::Proxy(const std::string name,
             const std::string expander_name,
             const std::string module_type,
             const Expander_ptr expander,
             const std::vector<ConstExpression_ptr> arguments)
    : Module(name), expander(expander) {
    this->properties = Module::get_module_defaults(module_type);
    this->properties["is_ready"] = std::make_shared<BooleanVariable>(false);

    if (this->expander->get_property("is_ready")->boolean_value()) {
        this->expander->send_proxy(name, module_type, arguments);
        this->properties["is_ready"]->set_boolean_value(true);
    } else {
        echo("%s: Expander not ready", this->name.c_str());
    }
    this->expander->add_proxy(this); // last: a throw above frees this proxy, and the expander must not keep a pointer to it
}

void Proxy::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    this->expander->send_call(this->name, method_name, arguments);
}

void Proxy::write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) {
    if (!this->properties.count(property_name)) {
        this->properties[property_name] = std::make_shared<Variable>(expression->type);
        echo("%s: Unknown property \"%s\"", this->name.c_str(), property_name.c_str());
    }
    if (!from_expander) {
        this->expander->send_property(this->name, property_name, expression);
    }
    Module::get_property(property_name)->assign(expression);
}

void Proxy::declare_property(const std::string &property_name, const Variable_ptr &variable) {
    if (property_name.find('.') != std::string::npos) {
        throw std::runtime_error("proxies do not support nested property \"" + this->name + "." + property_name + "\"");
    }
    const auto it = this->properties.find(property_name);
    if (it == this->properties.end()) {
        this->properties[property_name] = variable;
    } else if (it->second->type == variable->type) {
        it->second->assign(std::make_shared<VariableExpression>(variable));
    } else {
        throw std::runtime_error("property \"" + this->name + "." + property_name + "\" is " +
                                 describe(it->second->type) + ", not " + describe(variable->type));
    }
}
