#include "module.h"

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

void Module::call(std::string method)
{
    if (method == "mute")
    {
        this->output = false;
    }
    else if (method == "unmute")
    {
        this->output = true;
    }
    else
    {
        printf("error: unknown method \"%s\"\n", method.c_str());
    }
}

std::string Module::get_output()
{
    return "";
}