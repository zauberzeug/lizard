#include "core.h"

#include <stdlib.h>
#include "../global.h"
#include "../utils/output.h"
#include "../utils/strings.h"
#include "../utils/timing.h"

Core::Core(std::string name) : Module(core, name)
{
    this->properties["debug"] = new BooleanVariable(false);
    this->properties["millis"] = new IntegerVariable();
    this->properties["heap"] = new IntegerVariable();
}

void Core::step()
{
    this->properties["millis"]->integer_value = millis();
    this->properties["heap"]->integer_value = xPortGetFreeHeapSize();
    Module::step();
}

void Core::call(std::string method_name, std::vector<Expression *> arguments)
{
    if (method_name == "restart")
    {
        Module::expect(arguments, 0);
        esp_restart();
    }
    else if (method_name == "print")
    {
        static char buffer[1024];
        int pos = 0;
        for (auto const &argument : arguments)
        {
            if (argument != arguments[0])
            {
                pos += sprintf(&buffer[pos], " ");
            }
            pos += argument->print_to_buffer(&buffer[pos]);
        }
        echo(all, text, buffer);
    }
    else if (method_name == "output")
    {
        Module::expect(arguments, 1, string);
        this->output_list.clear();
        std::string format = arguments[0]->evaluate_string();
        while (!format.empty())
        {
            std::string element = cut_first_word(format);
            std::string module_name = cut_first_word(element, '.');
            Module *module = Global::get_module(module_name);
            std::string method_name = cut_first_word(element, ':');
            unsigned int precision = element.empty() ? 0 : atoi(element.c_str());
            this->output_list.push_back({module, method_name, precision});
        }
        this->output = true;
    }
    else
    {
        Module::call(method_name, arguments);
    }
}

std::string Core::get_output()
{
    static char format_buffer[8];
    static char output_buffer[1024];
    int pos = 0;
    for (auto const &element : this->output_list)
    {
        sprintf(format_buffer, "%%.%df", element.precision);
        Variable *property = element.module->get_property(element.property_name);
        switch (property->type)
        {
        case boolean:
            pos += sprintf(&output_buffer[pos], "%s", property->boolean_value ? "true" : "false");
            break;
        case integer:
            pos += sprintf(&output_buffer[pos], "%lld", property->integer_value);
            break;
        case number:
            pos += sprintf(&output_buffer[pos], format_buffer, property->number_value);
            break;
        case string:
            pos += sprintf(&output_buffer[pos], "\"%s\"", property->string_value.c_str());
            break;
        default:
            throw std::runtime_error("invalid property type");
        }
    }
    return std::string(output_buffer);
}
