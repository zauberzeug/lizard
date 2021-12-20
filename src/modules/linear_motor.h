#pragma once

#include "driver/gpio.h"
#include "module.h"

class LinearMotor : public Module {
private:
    const gpio_num_t move_in;
    const gpio_num_t move_out;
    const gpio_num_t end_in;
    const gpio_num_t end_out;

public:
    LinearMotor(const std::string name,
                const gpio_num_t move_in,
                const gpio_num_t move_out,
                const gpio_num_t end_in,
                const gpio_num_t end_out);
    void step();
    void call(const std::string method_name, const std::vector<const Expression *> arguments);
};
