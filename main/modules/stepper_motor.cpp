#include "stepper_motor.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "soc/gpio_sig_map.h"
#include <algorithm>
#include <driver/ledc.h>
#include <driver/pulse_cnt.h>
#include <math.h>
#include <memory>
#include <stdexcept>

#define MIN_SPEED 490

// changes https://docs.espressif.com/projects/esp-idf/en/stable/esp32/migration-guides/release-5.x/5.0/peripherals.html#id3
bool echo_if_error(const char *message, esp_err_t err) {
    if (err != ESP_OK) {
        const char *error_name = esp_err_to_name(err);
        echo("Error: %s in %s\n", error_name, message);
        return false;
    }
    return true;
}
StepperMotor::StepperMotor(const std::string &name,
                           const gpio_num_t step_pin,
                           const gpio_num_t dir_pin,
                           pcnt_unit_handle_t pcnt_unit,
                           pcnt_channel_handle_t pcnt_channel,
                           const ledc_timer_t ledc_timer,
                           const ledc_channel_t ledc_channel)
    : Module(stepper_motor, name),
      step_pin(step_pin),
      dir_pin(dir_pin),
      pcnt_unit(pcnt_unit),
      pcnt_channel(pcnt_channel),
      ledc_timer(ledc_timer),
      ledc_channel(ledc_channel) {
    gpio_reset_pin(step_pin);
    gpio_reset_pin(dir_pin);

    this->properties["position"] = std::make_shared<IntegerVariable>();
    this->properties["speed"] = std::make_shared<IntegerVariable>();
    this->properties["idle"] = std::make_shared<BooleanVariable>(true);

    pcnt_unit_config_t pcnt_unit_config = {
        .low_limit = -30000,
        .high_limit = 30000,
        .intr_priority = 0,
        .flags = {},
    };
    echo_if_error("new unit", pcnt_new_unit(&pcnt_unit_config, &this->pcnt_unit));

    pcnt_chan_config_t pcnt_channel_config = {
        .edge_gpio_num = step_pin,
        .level_gpio_num = dir_pin,
        .flags = {},
    };
    echo_if_error("new channel", pcnt_new_channel(this->pcnt_unit, &pcnt_channel_config, &this->pcnt_channel));
    // //  .hctrl_mode = PCNT_MODE_KEEP, .lctrl_mode = PCNT_MODE_REVERSE,
    // pcnt_channel_set_level_action(this->pcnt_channel, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    // // .pos_mode = PCNT_COUNT_INC, .neg_mode = PCNT_COUNT_DIS,
    // pcnt_channel_set_edge_action(this->pcnt_channel, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    echo_if_error("enable", pcnt_unit_enable(this->pcnt_unit));
    echo_if_error("clear count", pcnt_unit_clear_count(this->pcnt_unit));
    echo_if_error("start", pcnt_unit_start(this->pcnt_unit));
    echo_if_error("stop", pcnt_unit_stop(this->pcnt_unit));

    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_1_BIT,
        .timer_num = this->ledc_timer,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false, // check if this is correct
    };

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
    echo_if_error("timer config", ledc_timer_config(&timer_config));
    echo_if_error("ledc channel config", ledc_channel_config(&channel_config));
    echo_if_error("set step pin", gpio_set_direction(step_pin, GPIO_MODE_INPUT_OUTPUT));
    echo_if_error("set dir pin", gpio_set_direction(dir_pin, GPIO_MODE_INPUT_OUTPUT));
}

void StepperMotor::read_position() {
    int tempCount;
    echo_if_error("pcnt get count", pcnt_unit_get_count(this->pcnt_unit, &tempCount));
    int16_t count = static_cast<int16_t>(tempCount);
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

void StepperMotor::set_state(const StepperState new_state) {
    this->state = new_state;
    this->properties.at("idle")->boolean_value = (new_state == Idle);

    gpio_iomux_out(this->step_pin, new_state == Idle ? SIG_GPIO_OUT_IDX : LEDC_HS_SIG_OUT0_IDX + this->ledc_channel, 0); // needs to be checked especially
    echo_if_error("ledc set duty", ledc_set_duty(LEDC_HIGH_SPEED_MODE, this->ledc_channel, new_state == Idle ? 0 : 1));
    echo_if_error("update duty", ledc_update_duty(LEDC_HIGH_SPEED_MODE, this->ledc_channel));
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
        int32_t target_speed = this->target_speed;

        if (this->state == Positioning) {
            if (this->target_acceleration == 0) {
                if ((target_speed > 0 && position + dt * speed / 2 > this->target_position) ||
                    (target_speed < 0 && position + dt * speed / 2 < this->target_position)) {
                    this->target_speed = target_speed = 0;
                }
            } else {
                double squared_speed = static_cast<double>(speed) * speed;
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
            int32_t d_speed = std::max(dt * static_cast<double>(this->target_acceleration), 1.0);
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
