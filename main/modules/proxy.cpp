#include "proxy.h"
#include "../utils/string_utils.h"
#include "driver/uart.h"
#include <memory>

Proxy::Proxy(const std::string name,
             const std::string expander_name,
             const std::string module_type,
             const Expander_ptr expander,
             const std::vector<ConstExpression_ptr> arguments)
    : Module(proxy, name), expander(expander) {
    this->properties["is_ready"] = std::make_shared<BooleanVariable>(false);
    expander->add_proxy(name, module_type, arguments);
}

void Proxy::step() {
    // Update proxy's ready state based on expander's state
    this->properties.at("is_ready")->boolean_value =
        expander->get_property("is_ready")->boolean_value;
    Module::step();
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
    }
    if (!from_expander) {
        static char buffer[256];
        int pos = csprintf(buffer, sizeof(buffer), "%s.%s = ", this->name.c_str(), property_name.c_str());
        pos += expression->print_to_buffer(&buffer[pos], sizeof(buffer) - pos);
        this->expander->serial->write_checked_line(buffer, pos);
    }
    Module::get_property(property_name)->assign(expression);
}
