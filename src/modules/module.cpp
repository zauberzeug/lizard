#include "module.h"

#include <stdarg.h>
#include "driver/gpio.h"
#include "button.h"
#include "led.h"
#include "roboclaw.h"
#include "roboclaw_motor.h"
#include "serial.h"
#include "../global.h"

Module::Module(ModuleType type, std::string name)
{
    this->type = type;
    this->name = name;
}

void Module::Module::expect(std::vector<Expression *> arguments, int num, ...)
{
    if (arguments.size() != num)
    {
        throw std::runtime_error("expecting " + std::to_string(num) + " arguments, got " + std::to_string(arguments.size()));
    }
    va_list vl;
    va_start(vl, num);
    for (int i = 0; i < num; i++)
    {
        if (arguments[0]->type != va_arg(vl, int))
        {
            throw std::runtime_error("type mismatch at argument " + std::to_string(i));
        }
    }
    va_end(vl);
}

Module *Module::create(std::string type, std::string name, std::vector<Expression *> arguments)
{
    if (type == "Led")
    {
        Module::expect(arguments, 1, integer);
        return new Led(name, (gpio_num_t)arguments[0]->evaluate_integer());
    }
    else if (type == "Button")
    {
        Module::expect(arguments, 1, integer);
        return new Button(name, (gpio_num_t)arguments[0]->evaluate_integer());
    }
    else if (type == "Serial")
    {
        Module::expect(arguments, 4, integer, integer, integer, integer);
        gpio_num_t rx_pin = (gpio_num_t)arguments[0]->evaluate_integer();
        gpio_num_t tx_pin = (gpio_num_t)arguments[1]->evaluate_integer();
        long baud_rate = arguments[2]->evaluate_integer();
        gpio_port_t uart_num = (gpio_port_t)arguments[3]->evaluate_integer();
        return new Serial(name, rx_pin, tx_pin, baud_rate, uart_num);
    }
    else if (type == "RoboClaw")
    {
        Module::expect(arguments, 2, identifier, integer);
        std::string serial_name = arguments[0]->evaluate_identifier();
        Module *module = Global::get_module(serial_name);
        if (module->type != serial)
        {
            printf("error: module \"%s\" is no serial connection\n", serial_name.c_str());
            return nullptr;
        }
        Serial *serial = (Serial *)module;
        uint8_t address = arguments[1]->evaluate_integer();
        return new RoboClaw(name, serial, address);
    }
    else if (type == "RoboClawMotor")
    {
        Module::expect(arguments, 2, identifier, integer);
        std::string roboclaw_name = arguments[0]->evaluate_identifier();
        Module *module = Global::get_module(roboclaw_name);
        if (module->type != roboclaw)
        {
            printf("error: module \"%s\" is no RoboClaw\n", roboclaw_name.c_str());
            return nullptr;
        }
        RoboClaw *roboclaw = (RoboClaw *)module;
        unsigned int motor_number = arguments[1]->evaluate_integer();
        return new RoboClawMotor(name, roboclaw, motor_number);
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

void Module::call(std::string method_name, std::vector<Expression *> arguments)
{
    if (method_name == "mute")
    {
        Module::expect(arguments, 0);
        this->output = false;
    }
    else if (method_name == "unmute")
    {
        Module::expect(arguments, 0);
        this->output = true;
    }
    else if (method_name == "shadow")
    {
        Module::expect(arguments, 1, identifier);
        std::string target_name = arguments[0]->evaluate_identifier();
        Module *target_module = Global::get_module(target_name);
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
        printf("error: unknown method \"%s.%s\"\n", this->name.c_str(), method_name.c_str());
        return;
    }
}

void Module::call_with_shadows(std::string method_name, std::vector<Expression *> arguments)
{
    this->call(method_name, arguments);
    for (auto const &module : this->shadow_modules)
    {
        module->call(method_name, arguments);
    }
}

double Module::get(std::string property_name)
{
    printf("error: unknown property \"%s\"\n", property_name.c_str());
    return 0;
}

void Module::set(std::string property_name, double value)
{
    printf("error: unknown property \"%s\"\n", property_name.c_str());
}

std::string Module::get_output()
{
    return "";
}