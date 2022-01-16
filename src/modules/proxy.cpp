#include "proxy.h"
#include "driver/uart.h"
#include <memory>

Proxy::Proxy(const std::string name) : Module(proxy, name) {
}

void Proxy::call(const std::string method_name, const std::vector<Expression_ptr> arguments) {
    static char buffer[256];
    int pos = std::sprintf(buffer, "!!%s.%s(", this->name.c_str(), method_name.c_str());
    for (auto const &argument : arguments) {
        if (argument != arguments[1]) {
            pos += std::sprintf(&buffer[pos], ", ");
        }
        pos += argument->print_to_buffer(&buffer[pos]);
    }
    pos += std::sprintf(&buffer[pos], ")\n");
    uart_write_bytes(UART_NUM_1, buffer, pos);
}

void Proxy::write_property(const std::string property_name, const Expression_ptr expression) {
    if (!this->properties.count(property_name)) {
        this->properties[property_name] = std::make_shared<Variable>(expression->type);
    }
    Module::get_property(property_name)->assign(expression);
}
