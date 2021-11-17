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
    else
    {
        return Module::get(property_name);
    }
}
