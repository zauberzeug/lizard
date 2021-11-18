#include "core.h"

#include "esp_system.h"
#include "esp_timer.h"

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