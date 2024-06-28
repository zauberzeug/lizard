#include "proxy.h"
#include "driver/uart.h"
#include <memory>

Proxy::Proxy(const std::string name,
             const std::string expander_name,
             const std::string module_type,
             const Expander_ptr expander,
             const std::vector<ConstExpression_ptr> arguments)
    : Module(proxy, name), expander(expander) {
    static char buffer[256];
    int pos = std::sprintf(buffer, "%s = %s(", name.c_str(), module_type.c_str());
    pos += write_arguments_to_buffer(arguments, &buffer[pos]);
    pos += std::sprintf(&buffer[pos], "); ");
    pos += std::sprintf(&buffer[pos], "%s.broadcast()", name.c_str());

    expander->serial->write_checked_line(buffer, pos);
}

void Proxy::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    static char buffer[256];
    int pos = std::sprintf(buffer, "%s.%s(", this->name.c_str(), method_name.c_str());
    pos += write_arguments_to_buffer(arguments, &buffer[pos]);
    pos += std::sprintf(&buffer[pos], ")");
    this->expander->serial->write_checked_line(buffer, pos);
}

void Proxy::write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) {
    if (!this->properties.count(property_name)) {
        this->properties[property_name] = std::make_shared<Variable>(expression->type);
    }
    if (!from_expander) {
        static char buffer[256];
        int pos = std::sprintf(buffer, "%s.%s = ", this->name.c_str(), property_name.c_str());
        pos += expression->print_to_buffer(&buffer[pos]);
        this->expander->serial->write_checked_line(buffer, pos);
    }
    Module::get_property(property_name)->assign(expression);
}
