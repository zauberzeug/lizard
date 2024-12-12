#include "proxy.h"
#include "../utils/string_utils.h"
#include "../utils/uart.h"
#include "driver/uart.h"
#include <memory>
#include <stdexcept>
Proxy::Proxy(const std::string name,
             const std::string expander_name,
             const std::string module_type,
             const Expander_ptr expander,
             const std::vector<ConstExpression_ptr> arguments)
    : Module(proxy, name), expander(expander) {
    this->properties = Module::get_module_defaults(module_type);
    this->properties["is_ready"] = expander->get_property("is_ready");
    if (this->properties["is_ready"]->boolean_value == false) {
        throw std::runtime_error("Proxy: Expander \"" + expander->name + "\" is not ready");
    }
    this->expander->write_proxy(name, module_type, arguments);
}

void Proxy::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    this->expander->write_call(method_name, arguments);
}

void Proxy::write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) {
    if (!this->properties.count(property_name)) {
        this->properties[property_name] = std::make_shared<Variable>(expression->type);
        echo("%s: Unknown property \"%s\"", this->name.c_str(), property_name.c_str());
    }
    if (!from_expander) {
        this->expander->write_property(this->name, property_name, expression);
    }
    Module::get_property(property_name)->assign(expression);
}
