#include "proxy.h"

#include "driver/uart.h"

int print_argument_to_buffer(char *buffer, Expression *argument)
{
    switch (argument->type)
    {
    case boolean:
        return sprintf(buffer, "%s", argument->evaluate_boolean() ? "true" : "false");
    case integer:
        return sprintf(buffer, "%lld", argument->evaluate_integer());
    case number:
        return sprintf(buffer, "%f", argument->evaluate_number());
    case string:
        return sprintf(buffer, "\"%s\"", argument->evaluate_string().c_str());
    case identifier:
        return sprintf(buffer, "%s", argument->evaluate_identifier().c_str());
    default:
        throw std::runtime_error("argument has an invalid datatype");
    }
}

Proxy::Proxy(std::string name, std::vector<Expression *> arguments) : Module(proxy, name)
{
    static char buffer[256];
    std::string module_type = arguments[0]->evaluate_identifier();
    int pos = std::sprintf(buffer, "%s = %s(", name.c_str(), module_type.c_str());
    for (auto const &argument : arguments)
    {
        if (argument == arguments[0])
        {
            continue;
        }
        if (argument != arguments[1])
        {
            pos += std::sprintf(&buffer[pos], ", ");
        }
        pos += print_argument_to_buffer(&buffer[pos], argument);
    }
    pos += std::sprintf(&buffer[pos], ")\n");
    uart_write_bytes(UART_NUM_1, buffer, pos);
}

void Proxy::call(std::string method_name, std::vector<Expression *> arguments)
{
    static char buffer[256];
    int pos = std::sprintf(buffer, "%s.%s(", this->name.c_str(), method_name.c_str());
    for (auto const &argument : arguments)
    {
        if (argument != arguments[1])
        {
            pos += std::sprintf(&buffer[pos], ", ");
        }
        pos += print_argument_to_buffer(&buffer[pos], argument);
    }
    pos += std::sprintf(&buffer[pos], ")\n");
    uart_write_bytes(UART_NUM_1, buffer, pos);
}

void Proxy::write_property(std::string property_name, Expression *expression)
{
    if (!this->properties.count(property_name))
    {
        this->properties[property_name] = new Variable();
        this->properties[property_name]->type = expression->type;
    }
    Module::get_property(property_name)->assign(expression);
}
