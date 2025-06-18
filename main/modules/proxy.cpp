#include "proxy.h"
#include "../compilation/expressions.h"
#include "../utils/string_utils.h"
#include "../utils/uart.h"
#include "driver/uart.h"
#include <memory>

Proxy::Proxy(const std::string name,
             const std::string expander_name,
             const std::string module_type,
             const Expandable_ptr expandable,
             const std::vector<ConstExpression_ptr> arguments)
    : Module(proxy, name), expandable(expandable) {
    this->properties = Module::get_module_defaults(module_type);
    this->properties["is_ready"] = std::make_shared<BooleanVariable>(false);

    if (this->expandable->is_ready()) {
        this->expandable->send_proxy(name, module_type, arguments);
        this->properties["is_ready"]->boolean_value = true;
    } else {
        echo("%s: Expander not ready", this->name.c_str());
    }
}

void Proxy::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    this->expandable->send_call(this->name, method_name, arguments);
}

void Proxy::write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) {
    if (!this->properties.count(property_name)) {
        this->properties[property_name] = std::make_shared<Variable>(expression->type);
        echo("%s: Unknown property \"%s\"", this->name.c_str(), property_name.c_str());
    }
    if (!from_expander) {
        this->expandable->send_property(this->name, property_name, expression);
    }
    Module::get_property(property_name)->assign(expression);
}

void Proxy::send_proxy(const std::string module_name, const std::string module_type, const std::vector<ConstExpression_ptr> arguments) {
    // For proxy chaining, we always need to preserve the expander reference in the module type
    // The command should be: module_name = proxy_name.original_module_type(args)
    // This ensures the module gets created on the correct target, not the intermediate ESP32 (this proxy)
    std::string chained_module_type = this->name + "." + module_type;
    this->expandable->send_proxy(module_name, chained_module_type, arguments);
}

void Proxy::send_property(const std::string proxy_name, const std::string property_name, const ConstExpression_ptr expression) {
    this->expandable->send_property(proxy_name, property_name, expression);
}

void Proxy::send_call(const std::string proxy_name, const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    this->expandable->send_call(proxy_name, method_name, arguments);
}

bool Proxy::is_ready() const {
    return this->expandable->is_ready();
}
