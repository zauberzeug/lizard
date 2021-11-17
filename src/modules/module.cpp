#include "module.h"

#include "driver/gpio.h"
#include "button.h"
#include "led.h"
#include "roboclaw.h"
#include "serial.h"
#include "../global.h"

Module::Module(ModuleType type, std::string name)
{
    this->type = type;
    this->name = name;
}

Module *Module::create(std::string type, std::string name, std::vector<Argument *> arguments)
{
    if (type == "Led")
    {
        if (arguments.size() != 1 ||
            arguments[0]->type != integer)
        {
            printf("error: expecting 1 integer argument for \"Led\" constructor\n");
            return nullptr;
        }
        return new Led(name, (gpio_num_t)arguments[0]->integer_value);
    }
    else if (type == "Button")
    {
        if (arguments.size() != 1 ||
            arguments[0]->type != integer)
        {
            printf("error: expecting 1 integer argument for \"Button\" constructor\n");
            return nullptr;
        }
        return new Button(name, (gpio_num_t)arguments[0]->integer_value);
    }
    else if (type == "Serial")
    {
        if (arguments.size() != 4 ||
            arguments[0]->type != integer ||
            arguments[1]->type != integer ||
            arguments[2]->type != integer ||
            arguments[3]->type != integer)
        {
            printf("error: expecting 4 integer arguments for \"Serial\" constructor\n");
            return nullptr;
        }
        gpio_num_t rx_pin = (gpio_num_t)arguments[0]->integer_value;
        gpio_num_t tx_pin = (gpio_num_t)arguments[1]->integer_value;
        long baud_rate = arguments[2]->integer_value;
        gpio_port_t uart_num = (gpio_port_t)arguments[3]->integer_value;
        return new Serial(name, rx_pin, tx_pin, baud_rate, uart_num);
    }
    else if (type == "RoboClaw")
    {
        if (arguments.size() != 2 ||
            arguments[0]->type != identifier ||
            arguments[1]->type != integer)
        {
            printf("error: expecting 1 identifier argument and 1 integer argument for \"Serial\" constructor\n");
            return nullptr;
        }
        std::string serial_name = arguments[0]->identifier_value;
        if (!Global::modules.count(serial_name))
        {
            printf("error: unknown module \"%s\"\n", serial_name.c_str());
            return nullptr;
        }
        if (Global::modules[serial_name]->type != serial)
        {
            printf("error: module \"%s\" is no serial connection\n", serial_name.c_str());
            return nullptr;
        }
        Serial *serial = (Serial *)(Global::modules[serial_name]);
        uint8_t address = arguments[1]->integer_value;
        return new RoboClaw(name, serial, address);
    }
    else
    {
        printf("error: unknown module type \"%s\"\n", type.c_str());
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
    else if (method == "shadow")
    {
        if (arguments.size() != 1 ||
            arguments[0]->type != identifier)
        {
            printf("error: expecting 1 identifier argument for method \"%s.%s\"\n", this->name.c_str(), method.c_str());
            return;
        }
        std::string target_name = arguments[0]->identifier_value;
        if (!Global::modules.count(target_name))
        {
            printf("error: unknown module \"%s\"\n", target_name.c_str());
            return;
        }
        Module *target_module = Global::modules[target_name];
        if (this->type != target_module->type)
        {
            printf("error: shadow module is not of same type\n");
            return;
        }
        if (this != target_module)
        {
            this->shadow_modules.push_back(target_module);
        }
    }
    else
    {
        printf("error: unknown method \"%s.%s\"\n", this->name.c_str(), method.c_str());
        return;
    }
}

void Module::call_with_shadows(std::string method, std::vector<Argument *> arguments)
{
    this->call(method, arguments);
    for (auto const &module : this->shadow_modules)
    {
        module->call(method, arguments);
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