#include "stepper_motor.h"
#include "../utils/uart.h"
#include <driver/ledc.h>
#include <driver/pcnt.h>
#include <memory>

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
    uint32_t frequency = this->target_speed;
    gpio_set_level(this->dir_pin, frequency > 0 ? 1 : 0);
    ledc_set_freq(LEDC_HIGH_SPEED_MODE, this->ledc_timer, frequency);
}

void StepperMotor::step() {
    this->read_position();

    switch (this->state) {
    case Idle:
        break;
    case Starting:
        this->set_frequency();
        ledc_timer_resume(LEDC_HIGH_SPEED_MODE, this->ledc_timer);
        this->state = Running;
        break;
    case Running:
        this->set_frequency();
        break;
    case Stopping:
        ledc_timer_pause(LEDC_HIGH_SPEED_MODE, this->ledc_timer);
        this->state = Idle;
        break;
    }

    Module::step();
}

void StepperMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "speed") {
        Module::expect(arguments, 1, numbery);
        this->target_speed = arguments[0]->evaluate_number();
        this->state = this->target_speed != 0 ? Starting : Stopping;
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->state = Stopping;
    } else {
        Module::call(method_name, arguments);
    }
}
