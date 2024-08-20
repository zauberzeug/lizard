#include "proxy.h"
#include "driver/uart.h"
#include <memory>

Proxy::Proxy(const std::string name,
             const std::string expander_name,
             const std::string module_type,
             const Expander_ptr expander,
             const std::vector<ConstExpression_ptr> arguments)
    : Module(proxy, name), expander(expander), external_expander(nullptr) {
    static char buffer[256];
    int pos = std::sprintf(buffer, "%s = %s(", name.c_str(), module_type.c_str());
    pos += write_arguments_to_buffer(arguments, &buffer[pos]);
    pos += std::sprintf(&buffer[pos], "); ");
    pos += std::sprintf(&buffer[pos], "%s.broadcast()", name.c_str());

    // XXX The properties of the proxied module type don't actually exist
    // before the first expander broadcast, making them unusable for rule
    // definitions.
    if (module_type == "Input") {
        this->properties["level"] = std::make_shared<IntegerVariable>(0);
        this->properties["active"] = std::make_shared<BooleanVariable>(false);
    }

    expander->serial->write_checked_line(buffer, pos);
}

Proxy::Proxy(const std::string &name,
             const std::string &expander_name,
             const std::string &module_type,
             std::shared_ptr<ExternalExpander> external_expander,
             const std::vector<ConstExpression_ptr> &arguments)
    : Module(proxy, name), expander(nullptr), external_expander(external_expander) {
    static char buffer[256];
    int pos = std::sprintf(buffer, "%s = %s(", name.c_str(), module_type.c_str());
    pos += write_arguments_to_buffer(arguments, &buffer[pos]);
    pos += std::sprintf(&buffer[pos], "); ");
    pos += std::sprintf(&buffer[pos], "%s.broadcast()", name.c_str());

    if (module_type == "Input") {
        this->properties["level"] = std::make_shared<IntegerVariable>(0);
        this->properties["active"] = std::make_shared<BooleanVariable>(false);
    }

    external_expander->serial->write_checked_line_id(external_expander->device_id, buffer, pos);
}

void Proxy::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    static char buffer[256];
    int pos = std::sprintf(buffer, "%s.%s(", this->name.c_str(), method_name.c_str());
    pos += write_arguments_to_buffer(arguments, &buffer[pos]);
    pos += std::sprintf(&buffer[pos], ")");
    if (expander) {
        expander->serial->write_checked_line(buffer, pos);
    } else if (external_expander) {
        external_expander->serial->write_checked_line_id(external_expander->device_id, buffer, pos);
    }
}

void Proxy::write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) {
    if (!this->properties.count(property_name)) {
        this->properties[property_name] = std::make_shared<Variable>(expression->type);
    }
    if (!from_expander) {
        static char buffer[256];
        int pos = std::sprintf(buffer, "%s.%s = ", this->name.c_str(), property_name.c_str());
        pos += expression->print_to_buffer(&buffer[pos]);
        if (expander) {
            expander->serial->write_checked_line(buffer, pos);
        } else if (external_expander) {
            external_expander->serial->write_checked_line_id(external_expander->device_id, buffer, pos);
        }
    }
    Module::get_property(property_name)->assign(expression);
}
