#pragma once

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pcnt.h"
#include "module.h"

enum StepperState {
    Idle,
    Starting,
    Running,
    Stopping,
};

enum StepperMode {
    Speed,
    Position,
};

class StepperMotor : public Module {
private:
    const gpio_num_t step_pin;
    const gpio_num_t dir_pin;
    const pcnt_unit_t pcnt_unit;
    const pcnt_channel_t pcnt_channel;
    const ledc_timer_t ledc_timer;
    const ledc_channel_t ledc_channel;

    StepperState state = Idle;
    StepperState mode;
    double target_speed;

    void read_position();
    void set_frequency();

public:
    StepperMotor(const std::string name,
                 const gpio_num_t step_pin,
                 const gpio_num_t dir_pin,
                 const pcnt_unit_t pcnt_unit,
                 const pcnt_channel_t pcnt_channel,
                 const ledc_timer_t ledc_timer,
                 const ledc_channel_t ledc_channel);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
};
