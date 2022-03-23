#include "stepper_motor.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include <algorithm>
#include <driver/ledc.h>
#include <driver/pcnt.h>
#include <math.h>
#include <memory>

#define MIN_SPEED 490

StepperMotor::StepperMotor(const std::string name,
                           const gpio_num_t step_pin,
                           const gpio_num_t dir_pin,
                           const pcnt_unit_t pcnt_unit,
                           const pcnt_channel_t pcnt_channel,
                           const ledc_timer_t ledc_timer,
                           const ledc_channel_t ledc_channel)
    : Module(output, name),
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
    ledc_channel_config_t channel_config = {
        .gpio_num = step_pin,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = this->ledc_channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = this->ledc_timer,
        .duty = 1,
        .hpoint = 0,
        .flags = {},
    };
    ledc_timer_config(&timer_config);
    ledc_channel_config(&channel_config);
    ledc_timer_pause(LEDC_HIGH_SPEED_MODE, this->ledc_timer);
    gpio_set_direction(step_pin, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(dir_pin, GPIO_MODE_INPUT_OUTPUT);
    gpio_matrix_out(step_pin, LEDC_HS_SIG_OUT0_IDX + this->ledc_channel, 0, 0);
}

void StepperMotor::read_position() {
    static int16_t last_count = 0;
    int16_t count;
    pcnt_get_counter_value(this->pcnt_unit, &count);
    int16_t d_count = count - last_count;
    if (d_count > 15000) {
        d_count -= 30000;
    }
    if (d_count < -15000) {
        d_count += 30000;
    }
    this->properties.at("position")->integer_value += d_count;
    last_count = count;
}

void StepperMotor::set_frequency() {
    static uint32_t last_micros = 0;
    double dt = micros_since(last_micros) * 1e-6;
    last_micros = micros();

    int32_t speed = this->properties.at("speed")->integer_value;
    int32_t d_speed = std::max(dt * (double)this->target_acceleration, 1.0);
    if (this->target_acceleration == 0) {
        speed = this->target_speed;
    } else if (speed < this->target_speed) {
        speed = std::min(speed + d_speed, this->target_speed);
    } else if (speed > this->target_speed) {
        speed = std::max(speed - d_speed, this->target_speed);
    }
    if (-MIN_SPEED < speed && speed < MIN_SPEED) {
        ledc_timer_pause(LEDC_HIGH_SPEED_MODE, this->ledc_timer);
    } else {
        gpio_set_level(this->dir_pin, speed > 0 ? 1 : 0);
        ledc_set_freq(LEDC_HIGH_SPEED_MODE, this->ledc_timer, std::abs(speed));
        ledc_timer_resume(LEDC_HIGH_SPEED_MODE, this->ledc_timer);
    }
    this->properties.at("speed")->integer_value = speed;
}

void StepperMotor::step() {
    this->read_position();

    if (this->is_running) {
        this->set_frequency();
    }

    Module::step();
}

void StepperMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "speed") {
        if (arguments.size() < 1 && arguments.size() > 2) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, numbery, numbery);
        int32_t target_speed = arguments[0]->evaluate_number();
        if (target_speed > 0 && target_speed < MIN_SPEED) {
            throw std::runtime_error("a target speed below " + std::to_string(MIN_SPEED) + " steps per second is not supported");
        }
        this->target_speed = target_speed;
        this->target_acceleration = arguments.size() > 1 ? std::abs(arguments[1]->evaluate_number()) : 0;
        this->is_running = true;
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        ledc_timer_pause(LEDC_HIGH_SPEED_MODE, this->ledc_timer);
        this->is_running = false;
    } else {
        Module::call(method_name, arguments);
    }
}
