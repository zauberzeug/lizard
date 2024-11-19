#include "proxy.h"
#include "../utils/echo.h"
#include "../utils/string_utils.h"
#include "driver/uart.h"
#include <memory>

Proxy::Proxy(const std::string name,
             const std::string expander_name,
             const std::string module_type,
             const Expander_ptr expander,
             const std::vector<ConstExpression_ptr> arguments)
    : Module(proxy, name), expander(expander), module_type(module_type) {
    this->cached_default_properties = Module::get_module_defaults(module_type);
    this->properties = this->cached_default_properties;
    this->properties["is_ready"] = expander->get_property("is_ready");
    expander->add_proxy(name, module_type, arguments);
}

void Proxy::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    static char buffer[256];
    int pos = csprintf(buffer, sizeof(buffer), "%s.%s(", this->name.c_str(), method_name.c_str());
    pos += write_arguments_to_buffer(arguments, &buffer[pos], sizeof(buffer) - pos);
    pos += csprintf(&buffer[pos], sizeof(buffer) - pos, ")");
    this->expander->serial->write_checked_line(buffer, pos);
}

void Proxy::write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) {
    if (!this->properties.count(property_name)) {
        this->properties[property_name] = std::make_shared<Variable>(expression->type);
        echo("%s: Unknown property %s in module type %s", property_name.c_str(), this->module_type.c_str());
    }

    if (!from_expander) {
        static char buffer[256];
        int pos = csprintf(buffer, sizeof(buffer), "%s.%s = ", this->name.c_str(), property_name.c_str());
        pos += expression->print_to_buffer(&buffer[pos], sizeof(buffer) - pos);
        this->expander->serial->write_checked_line(buffer, pos);
    }
    Module::get_property(property_name)->assign(expression);
}