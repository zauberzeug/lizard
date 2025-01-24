#pragma once

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "module.h"

class PwmOutput : public Module {
private:
    const gpio_num_t pin;
    const ledc_timer_t ledc_timer;
    const ledc_channel_t ledc_channel;
    bool is_on = false;

public:
    PwmOutput(const std::string name,
              const gpio_num_t pin,
              const ledc_timer_t ledc_timer,
              const ledc_channel_t ledc_channel);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
    void set_error_descriptions();
};
