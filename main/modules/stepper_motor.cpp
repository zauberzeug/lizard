#include "stepper_motor.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "esp32/rom/gpio.h"
#include "soc/gpio_sig_map.h"
#include <algorithm>
#include <driver/ledc.h>
#include <driver/pcnt.h>
#include <esp_rom_gpio.h>
#include <math.h>
#include <memory>
#include <stdexcept>

#define MIN_SPEED 490

std::map<std::string, Variable_ptr> StepperMotor::get_default_properties() const {
    return {
        {"position", std::make_shared<IntegerVariable>()},
        {"speed", std::make_shared<IntegerVariable>()},
        {"idle", std::make_shared<BooleanVariable>(true)}};
}

StepperMotor::StepperMotor(const std::string name,
                           const gpio_num_t step_pin,
                           const gpio_num_t dir_pin,
                           const pcnt_unit_t pcnt_unit,
                           const pcnt_channel_t pcnt_channel,
                           const ledc_timer_t ledc_timer,
                           const ledc_channel_t ledc_channel)
    : Module(stepper_motor, name),
      step_pin(step_pin),
      dir_pin(dir_pin),
      pcnt_unit(pcnt_unit),
      pcnt_channel(pcnt_channel),
      ledc_timer(ledc_timer),
      ledc_channel(ledc_channel) {
    this->properties = this->get_default_properties();

    gpio_reset_pin(step_pin);
    gpio_reset_pin(dir_pin);

    pcnt_config_t pcnt_config = {
        .pulse_gpio_num = step_pin,
        .ctrl_gpio_num = dir_pin,
        .lctrl_mode = PCNT_MODE_REVERSE,
        .hctrl_mode = PCNT_MODE_KEEP,
        .pos_mode = PCNT_COUNT_INC,
        .neg_mode = PCNT_COUNT_DIS,
        .counter_h_lim = 30000,
        .counter_l_lim = -30000,
        .unit = this->pcnt_unit,
        .channel = this->pcnt_channel,
    };
    pcnt_unit_config(&pcnt_config);
    pcnt_counter_pause(this->pcnt_unit);
    pcnt_counter_clear(this->pcnt_unit);
    pcnt_counter_resume(this->pcnt_unit);

    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_1_BIT,
        .timer_num = this->ledc_timer,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_config);

    ledc_channel_config_t channel_config = {
        .gpio_num = step_pin,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = this->ledc_channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = this->ledc_timer,
        .duty = 0,
        .hpoint = 0,
        .flags = {},
    };
    ledc_channel_config(&channel_config);

    gpio_set_direction(step_pin, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(dir_pin, GPIO_MODE_INPUT_OUTPUT);
}

void StepperMotor::read_position() {
    int16_t count;
    pcnt_get_counter_value(this->pcnt_unit, &count);
    int16_t d_count = count - this->last_count;
    if (d_count > 15000) {
        d_count -= 30000;
    }
    if (d_count < -15000) {
        d_count += 30000;
    }
    this->properties.at("position")->integer_value += d_count;
    this->last_count = count;
}

void StepperMotor::set_state(StepperState new_state) {
    this->state = new_state;
    this->properties.at("idle")->boolean_value = (new_state == Idle);

    gpio_matrix_out(this->step_pin, new_state == Idle ? SIG_GPIO_OUT_IDX : LEDC_HS_SIG_OUT0_IDX + this->ledc_channel, 0, 0);
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, this->ledc_channel, new_state == Idle ? 0 : 1);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, this->ledc_channel);
}

void StepperMotor::step() {
    this->read_position();

    // time since last call
    double dt = micros_since(this->last_micros) * 1e-6;
    this->last_micros = micros();

    if (this->state != Idle) {
        // current state
        int32_t position = this->properties.at("position")->integer_value;
        int32_t speed = this->properties.at("speed")->integer_value;

        // current target speed
        int32_t target_speed = this->target_speed;
        if (this->state == Positioning) {
            if (this->target_acceleration == 0) {
                if ((target_speed > 0 && position + dt * speed / 2 > this->target_position) ||
                    (target_speed < 0 && position + dt * speed / 2 < this->target_position)) {
                    this->target_speed = target_speed = 0;
                }
            } else {
                double squared_speed = (double)speed * speed;
                int32_t braking_distance = squared_speed / this->target_acceleration / 2.0;
                int32_t remaining_distance = this->target_position - position;
                if (std::abs(remaining_distance) < std::abs(braking_distance)) {
                    this->target_speed = target_speed = 0;
                }
            }
        }

        // update speed based on target speed and target acceleration
        if (this->target_acceleration == 0) {
            speed = target_speed;
        } else {
            int32_t d_speed = std::max(dt * (double)this->target_acceleration, 1.0);
            if (speed < target_speed) {
                speed = std::min(speed + d_speed, target_speed);
            } else if (speed > target_speed) {
                speed = std::max(speed - d_speed, target_speed);
            }
        }

        // set frequency and pause/resume LED controller
        int32_t abs_speed = std::abs(speed);
        if (abs_speed < MIN_SPEED) {
            ledc_timer_pause(LEDC_HIGH_SPEED_MODE, this->ledc_timer);
        } else {
            ledc_set_freq(LEDC_HIGH_SPEED_MODE, this->ledc_timer, abs_speed);
            ledc_timer_resume(LEDC_HIGH_SPEED_MODE, this->ledc_timer);
        }
        gpio_set_level(this->dir_pin, speed > 0 ? 1 : 0);

        // stop if target is reached
        if (target_speed == 0 && abs_speed < MIN_SPEED) {
            set_state(Idle);
        }

        this->properties.at("speed")->integer_value = speed;
    } else {
        this->properties.at("speed")->integer_value = 0;
    }

    Module::step();
}

void StepperMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "position") {
        if (arguments.size() < 2 || arguments.size() > 3) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, numbery, numbery, numbery);
        this->position(arguments[0]->evaluate_number(),
                       arguments[1]->evaluate_number(),
                       arguments.size() > 2 ? std::abs(arguments[2]->evaluate_number()) : 0);
    } else if (method_name == "speed") {
        if (arguments.size() < 1 || arguments.size() > 2) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, numbery, numbery);
        this->speed(arguments[0]->evaluate_number(),
                    arguments.size() > 1 ? std::abs(arguments[1]->evaluate_number()) : 0);
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->stop();
    } else {
        Module::call(method_name, arguments);
    }
}

void StepperMotor::stop() {
    set_state(Idle);
}

double StepperMotor::get_position() {
    return static_cast<double>(this->properties.at("position")->integer_value);
}

void StepperMotor::position(const double position, const double speed, const double acceleration) {
    this->target_position = static_cast<int32_t>(position);
    bool forward = this->target_position > this->properties.at("position")->integer_value;
    this->target_speed = static_cast<int32_t>(speed) * (forward ? 1 : -1);
    this->target_acceleration = static_cast<uint32_t>(acceleration);
    set_state(Positioning);
}

double StepperMotor::get_speed() {
    return static_cast<double>(this->properties.at("speed")->integer_value);
}

void StepperMotor::speed(const double speed, const double acceleration) {
    this->target_speed = static_cast<int32_t>(speed);
    this->target_acceleration = static_cast<uint32_t>(acceleration);
    set_state(this->target_speed == 0 ? Idle : Speeding);
}
