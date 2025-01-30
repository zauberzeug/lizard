#pragma once

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pcnt.h"
#include "module.h"
#include "motor.h"

enum StepperState {
    Idle,
    Speeding,
    Positioning,
};

class StepperMotor;
using StepperMotor_ptr = std::shared_ptr<StepperMotor>;

class StepperMotor : public Module, virtual public Motor {
private:
    const gpio_num_t step_pin;
    const gpio_num_t dir_pin;
    const pcnt_unit_t pcnt_unit;
    const pcnt_channel_t pcnt_channel;
    const ledc_timer_t ledc_timer;
    const ledc_channel_t ledc_channel;

    uint32_t last_micros = 0;
    int16_t last_count = 0;

    StepperState state = Idle;
    int32_t target_position = 0;
    int32_t target_speed = 0;
    uint32_t target_acceleration = 0;

    void read_position();
    void set_state(StepperState new_state);

protected:
    void set_error_descriptions() override;

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
    static const std::map<std::string, Variable_ptr> get_defaults();

    StepperState get_state() const { return this->state; }
    int32_t get_target_position() const { return this->target_position; }
    int32_t get_target_speed() const { return this->target_speed; }
    uint32_t get_target_acceleration() const { return this->target_acceleration; }

    void stop() override;
    double get_position() override;
    void position(const double position, const double speed, const double acceleration) override;
    double get_speed() override;
    void speed(const double speed, const double acceleration) override;
};
