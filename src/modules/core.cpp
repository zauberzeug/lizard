#include "core.h"

#include <stdlib.h>
#include "esp_system.h"
#include "esp_timer.h"
#include "../global.h"
#include "../utils/strings.h"

Core::Core(std::string name) : Module(core, name)
{
}

void Core::call(std::string method, std::vector<Argument *> arguments)
{
    if (method == "restart")
    {
        if (arguments.size() != 0)
        {
            printf("error: expecting no arguments for method \"%s.%s\"\n", this->name.c_str(), method.c_str());
            return;
        }
        esp_restart();
    }
    else if (method == "print")
    {
        if (arguments.size() != 1 ||
            arguments[0]->type != string)
        {
            printf("error: expecting 1 string argument for method \"%s.%s\"\n", this->name.c_str(), method.c_str());
            return;
        }
        printf("%s\n", arguments[0]->string_value.c_str());
    }
    else if (method == "output")
    {
        if (arguments.size() != 1 ||
            arguments[0]->type != string)
        {
            printf("error: expecting 1 string argument for method \"%s.%s\"\n", this->name.c_str(), method.c_str());
            return;
        }
        this->output_list.clear();
        while (!arguments[0]->string_value.empty())
        {
            std::string element = cut_first_word(arguments[0]->string_value);
            std::string module_name = cut_first_word(element, '.');
            if (!Global::modules.count(module_name))
            {
                printf("error: unknown module \"%s\"\n", module_name.c_str());
                return;
            }
            Module *module = Global::modules[module_name];
            std::string method_name = cut_first_word(element, ':');
            unsigned int precision = element.empty() ? 0 : atoi(element.c_str());
            this->output_list.push_back({module, method_name, precision});
        }
        this->output = true;
    }
    else
    {
        Module::call(method, arguments);
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
