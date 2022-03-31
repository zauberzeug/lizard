#pragma once

#include "driver/gpio.h"
#include "module.h"

class Output : public Module {
private:
    const gpio_num_t number;
    int target_level = 0;
    double pulse_interval = 0.0;
    double pulse_duty_cycle = 0.5;

public:
    Output(const std::string name, const gpio_num_t number);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
};
