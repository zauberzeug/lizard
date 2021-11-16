#include "module.h"

#include "driver/gpio.h"
#include "button.h"
#include "led.h"
#include "roboclaw.h"
#include "serial.h"

Module *Module::create(std::string module_type,
                       std::string module_name,
                       std::vector<Argument *> arguments,
                       std::map<std::string, Module *> existing_modules)
{
    if (module_type == "Led")
    {
        if (arguments.size() != 1 ||
            arguments[0]->type != number)
        {
            printf("error: expecting 1 number argument for \"Led\" constructor\n");
            return nullptr;
        }
        return new Led(module_name, (gpio_num_t)arguments[0]->number_value);
    }
    else if (module_type == "Button")
    {
        if (arguments.size() != 1 ||
            arguments[0]->type != number)
        {
            printf("error: expecting 1 number argument for \"Button\" constructor\n");
            return nullptr;
        }
        return new Button(module_name, (gpio_num_t)arguments[0]->number_value);
    }
    else if (module_type == "Serial")
    {
        if (arguments.size() != 4 ||
            arguments[0]->type != number ||
            arguments[1]->type != number ||
            arguments[2]->type != number ||
            arguments[3]->type != number)
        {
            printf("error: expecting 4 number arguments for \"Serial\" constructor\n");
            return nullptr;
        }
        gpio_num_t rx_pin = (gpio_num_t)arguments[0]->number_value;
        gpio_num_t tx_pin = (gpio_num_t)arguments[1]->number_value;
        long baud_rate = arguments[2]->number_value;
        gpio_port_t uart_num = (gpio_port_t)arguments[3]->number_value;
        return new Serial(module_name, rx_pin, tx_pin, baud_rate, uart_num);
    }
    else if (module_type == "RoboClaw")
    {
        if (arguments.size() != 2 ||
            arguments[0]->type != identifier ||
            arguments[1]->type != number)
        {
            printf("error: expecting 1 identifier argument and 1 number argument for \"Serial\" constructor\n");
            return nullptr;
        }
        std::string serial_name = arguments[0]->identifier_value;
        if (!existing_modules.count(serial_name))
        {
            printf("error: unknown module \"%s\"\n", serial_name.c_str());
            return nullptr;
        }
        Serial *serial = (Serial *)(existing_modules[serial_name]);
        uint8_t address = arguments[1]->number_value;
        return new RoboClaw(module_name, serial, address);
    }
    else
    {
        printf("error: unknown module type \"%s\"\n", module_type.c_str());
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