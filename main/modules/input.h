#pragma once

#include "driver/gpio.h"
#include "module.h"

class Input : public Module {
private:
    const gpio_num_t number;

public:
    Input(const std::string name, const gpio_num_t number);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    std::string get_output() const override;
};
