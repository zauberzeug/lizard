#include "stepper_motor.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "rom/gpio.h"
#include "soc/gpio_sig_map.h"
#include <algorithm>
#include <driver/ledc.h>
#include <stdexcept>

#define MIN_SPEED 490

static void check_error(esp_err_t err, const char *msg) {
    if (err != ESP_OK) {
        throw std::runtime_error(std::string(msg) + ": " + esp_err_to_name(err));
    }
}

#ifdef CONFIG_IDF_TARGET_ESP32S3
#define SPEED_MODE LEDC_LOW_SPEED_MODE
#define SPEED_OUT_IDX LEDC_LS_SIG_OUT0_IDX
#define DUTY_RESOLUTION LEDC_TIMER_8_BIT
#define DUTY_VALUE 128 // 50% of 256 with 8-bit resolution
#else
#define SPEED_MODE LEDC_HIGH_SPEED_MODE
#define SPEED_OUT_IDX LEDC_HS_SIG_OUT0_IDX
#define DUTY_RESOLUTION LEDC_TIMER_1_BIT
#define DUTY_VALUE 1 // 50% of max 1 with 1-bit resolution
#endif

REGISTER_MODULE_DEFAULTS(StepperMotor)

const std::map<std::string, Variable_ptr> StepperMotor::get_defaults() {
    return {
        {"position", std::make_shared<IntegerVariable>()},
        {"speed", std::make_shared<IntegerVariable>()},
        {"idle", std::make_shared<BooleanVariable>(true)},
        {"enabled", std::make_shared<BooleanVariable>(true)},
    };
}

StepperMotor::StepperMotor(const std::string name,
                           const gpio_num_t step_pin,
                           const gpio_num_t dir_pin,
                           const ledc_timer_t ledc_timer,
                           const ledc_channel_t ledc_channel)
    : Module(stepper_motor, name),
      step_pin(step_pin),
      dir_pin(dir_pin),
      ledc_timer(ledc_timer),
      ledc_channel(ledc_channel) {
    gpio_reset_pin(step_pin);
    gpio_reset_pin(dir_pin);

    this->properties = StepperMotor::get_defaults();

    pcnt_unit_config_t unit_config = {};
    unit_config.high_limit = 30000;
    unit_config.low_limit = -30000;
    check_error(pcnt_new_unit(&unit_config, &this->pcnt_unit), "failed to create PCNT unit");

    pcnt_chan_config_t chan_config = {};
    chan_config.edge_gpio_num = step_pin;
    chan_config.level_gpio_num = dir_pin;
    check_error(pcnt_new_channel(this->pcnt_unit, &chan_config, &this->pcnt_channel), "failed to create PCNT channel");

    check_error(pcnt_channel_set_edge_action(this->pcnt_channel,
                                             PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                             PCNT_CHANNEL_EDGE_ACTION_HOLD),
                "failed to set PCNT edge action");

    check_error(pcnt_channel_set_level_action(this->pcnt_channel,
                                              PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                              PCNT_CHANNEL_LEVEL_ACTION_INVERSE),
                "failed to set PCNT level action");

    check_error(pcnt_unit_enable(this->pcnt_unit), "failed to enable PCNT unit");
    check_error(pcnt_unit_clear_count(this->pcnt_unit), "failed to clear PCNT count");
    check_error(pcnt_unit_start(this->pcnt_unit), "failed to start PCNT unit");

    ledc_timer_config_t timer_config = {
        .speed_mode = SPEED_MODE,
        .duty_resolution = DUTY_RESOLUTION,
        .timer_num = this->ledc_timer,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    check_error(ledc_timer_config(&timer_config), "failed to configure LEDC timer");

    ledc_channel_config_t channel_config = {
        .gpio_num = step_pin,
        .speed_mode = SPEED_MODE,
        .channel = this->ledc_channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = this->ledc_timer,
        .duty = 0,
        .hpoint = 0,
        .flags = {},
    };
    check_error(ledc_channel_config(&channel_config), "failed to configure LEDC channel");

    gpio_set_direction(step_pin, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(dir_pin, GPIO_MODE_INPUT_OUTPUT);
}

void StepperMotor::read_position() {
    int count;
    pcnt_unit_get_count(this->pcnt_unit, &count);
    int d_count = count - this->last_count;
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

    gpio_matrix_out(this->step_pin, new_state == Idle ? SIG_GPIO_OUT_IDX : SPEED_OUT_IDX + this->ledc_channel, 0, 0);
    ledc_set_duty(SPEED_MODE, this->ledc_channel, new_state == Idle ? 0 : DUTY_VALUE);
    ledc_update_duty(SPEED_MODE, this->ledc_channel);
}

void StepperMotor::step() {
    this->read_position();

    // time since last call
    double dt = micros_since(this->last_micros) * 1e-6;
    this->last_micros = micros();

    if (this->properties.at("enabled")->boolean_value != this->enabled) {
        if (this->properties.at("enabled")->boolean_value) {
            this->enable();
        } else {
            this->disable();
        }
    }

    if (this->state != Idle && this->enabled) {
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
                uint32_t braking_distance = squared_speed / this->target_acceleration / 2.0;
                int32_t remaining_distance = (this->target_position - position) * (this->target_speed > 0 ? 1 : -1);
                if (remaining_distance < (int32_t)braking_distance) {
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
            ledc_timer_pause(SPEED_MODE, this->ledc_timer);
        } else {
            ledc_set_freq(SPEED_MODE, this->ledc_timer, abs_speed);
            ledc_timer_resume(SPEED_MODE, this->ledc_timer);
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
        if (this->enabled) {
            this->position(arguments[0]->evaluate_number(),
                           arguments[1]->evaluate_number(),
                           arguments.size() > 2 ? std::abs(arguments[2]->evaluate_number()) : 0);
        }
    } else if (method_name == "speed") {
        if (arguments.size() < 1 || arguments.size() > 2) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, numbery, numbery);
        if (this->enabled) {
            this->speed(arguments[0]->evaluate_number(),
                        arguments.size() > 1 ? std::abs(arguments[1]->evaluate_number()) : 0);
        }
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->stop();
    } else if (method_name == "enable") {
        Module::expect(arguments, 0);
        this->enable();
    } else if (method_name == "disable") {
        Module::expect(arguments, 0);
        this->disable();
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

void StepperMotor::enable() {
    this->enabled = true;
    this->properties.at("enabled")->boolean_value = true;
}

void StepperMotor::disable() {
    this->stop();
    this->enabled = false;
    this->properties.at("enabled")->boolean_value = false;
}
