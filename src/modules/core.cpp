#include "core.h"

#include <stdlib.h>
#include "esp_system.h"
#include "esp_timer.h"
#include "../global.h"
#include "../utils/strings.h"

Core::Core(std::string name) : Module(core, name)
{
}

void Core::call(std::string method_name, std::vector<Expression *> arguments)
{
    if (method_name == "restart")
    {
        if (arguments.size() != 0)
        {
            printf("error: expecting no arguments for method \"%s.%s\"\n", this->name.c_str(), method_name.c_str());
            return;
        }
        esp_restart();
    }
    else if (method_name == "print")
    {
        for (auto const &argument : arguments)
        {
            switch (argument->type)
            {
            case boolean:
                printf(argument->evaluate_boolean() ? "true" : "false");
                break;
            case integer:
                printf("%d", argument->evaluate_integer());
                break;
            case number:
                printf("%f", argument->evaluate_number());
                break;
            case identifier:
                printf(argument->evaluate_identifier().c_str());
                break;
            case string:
                printf("\"%s\"", argument->evaluate_string().c_str());
                break;
            }
        }
        printf("\n");
    }
    else if (method_name == "output")
    {
        if (arguments.size() != 1 ||
            arguments[0]->type != string)
        {
            printf("error: expecting 1 string argument for method \"%s.%s\"\n", this->name.c_str(), method_name.c_str());
            return;
        }
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

double Core::get(std::string property_name)
{
    if (property_name == "millis")
    {
        return esp_timer_get_time() / 1000ULL;
    }
    if (property_name == "debug")
    {
        return this->debug;
    }
    else
    {
        return Module::get(property_name);
    }
}

void Core::set(std::string property_name, double value)
{
    if (property_name == "debug")
    {
        this->debug = value;
    }
    else
    {
        Module::set(property_name, value);
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
        pos += sprintf(&output_buffer[pos], format_buffer, element.module->get(element.property_name));
    }
    return std::string(output_buffer);
}
