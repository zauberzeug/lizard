#pragma once

#include "driver/gpio.h"
#include "module.h"

class Output : public Module {
private:
    const gpio_num_t number;

public:
    Output(const std::string name, const gpio_num_t number);
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments);
};
