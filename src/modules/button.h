#pragma once

#include "driver/gpio.h"
#include "module.h"

class Button : public Module
{
private:
    const gpio_num_t number;

public:
    Button(const std::string name, const gpio_num_t number);
    void step();
    void call(const std::string method_name, const std::vector<const Expression *> arguments);
    std::string get_output() const;
};
