#include "proxy.h"
#include "../utils/string_utils.h"
#include "../utils/uart.h"
#include "driver/uart.h"
#include <algorithm>
#include <memory>

Proxy::Proxy(const std::string name,
             const std::string expander_name,
             const std::string module_type,
             const Expander_ptr expander,
             const std::vector<ConstExpression_ptr> arguments)
    : Module(proxy, name), expander(expander) {
    this->properties = Module::get_module_defaults(module_type);
    this->properties["is_ready"] = std::make_shared<BooleanVariable>(false);

    if (this->expander->get_property("is_ready")->boolean_value) {
        this->expander->send_proxy(name, module_type, arguments);
        this->properties["is_ready"]->boolean_value = true;
    } else {
        echo("%s: Expander not ready", this->name.c_str());
    }
}

void Proxy::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "subscribe" && arguments.size() >= 2) {
        try {
            std::string local_name;
            if (arguments.size() >= 3) {
                local_name = arguments[2]->evaluate_string();
            } else {
                local_name = arguments[1]->evaluate_string();
                std::replace(local_name.begin(), local_name.end(), '.', '_');
            }
            if (!this->properties.count(local_name)) {
                this->properties[local_name] = std::make_shared<NumberVariable>();
            }
            this->expander->send_call(this->name, "broadcast", {});
        } catch (...) {}
    }
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
