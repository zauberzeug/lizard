#include "module.h"

#include "button.h"
#include "led.h"

Module *Module::create(std::string module_type_string, std::vector<Argument *> arguments)
{
    if (module_type_string == "Led")
    {
        if (arguments.size() != 1 || arguments[0]->type != integer)
        {
            printf("error: expecting 1 integer argument for \"Led\" constructor\n");
            return nullptr;
        }
        return new Led((gpio_num_t)arguments[0]->integer_value);
    }
    else if (module_type_string == "Button")
    {
        if (arguments.size() != 1 || arguments[0]->type != integer)
        {
            printf("error: expecting 1 integer argument for \"Button\" constructor\n");
            return nullptr;
        }
        return new Button((gpio_num_t)arguments[0]->integer_value);
    }
    else
    {
        printf("error: unknown module type \"%s\"\n", module_type_string.c_str());
        return nullptr;
    }
}

void Module::step()
{
    if (this->output)
    {
        std::string output = this->get_output();
        if (!output.empty())
        {
            printf("%s %s\n", this->name.c_str(), output.c_str());
        }
    }
}

void Module::call(std::string method, std::vector<Argument *> arguments)
{
    if (method == "mute")
    {
        if (arguments.size() != 0)
        {
            printf("error: expecting no arguments for method \"%s.%s\"\n", this->name.c_str(), method.c_str());
            return;
        }
        this->output = false;
    }
    else if (method == "unmute")
    {
        if (arguments.size() != 0)
        {
            printf("error: expecting no arguments for method \"%s.%s\"\n", this->name.c_str(), method.c_str());
            return;
        }
        this->output = true;
    }
    else
    {
        printf("error: unknown method \"%s\"\n", method.c_str());
    }
}

double Module::get(std::string property_name)
{
    printf("error: unknown property \"%s\"\n", property_name.c_str());
    return 0;
}

std::string Module::get_output()
{
    return "";
}