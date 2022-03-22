#pragma once

#include "driver/gpio.h"
#include "module.h"

class StepperMotor : public Module {
private:
    const gpio_num_t step_pin;
    const gpio_num_t dir_pin;

public:
    StepperMotor(const std::string name, const gpio_num_t step_pin, const gpio_num_t dir_pin);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
};
