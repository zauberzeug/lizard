#include "pwm_output.h"
#include <driver/ledc.h>

const std::map<std::string, Variable_ptr> PwmOutput::get_defaults() {
    return {
        {"frequency", std::make_shared<IntegerVariable>(1000)},
        {"duty", std::make_shared<IntegerVariable>(128)},
    };
}

void PwmOutput::set_error_descriptions() {
    this->error_descriptions = {
        {0x01, "Could not configure PWM"},
        {0x02, "Could not update PWM"},
    };
}

PwmOutput::PwmOutput(const std::string name,
                     const gpio_num_t pin,
                     const ledc_timer_t ledc_timer,
                     const ledc_channel_t ledc_channel)
    : Module(pwm_output, name), pin(pin), ledc_timer(ledc_timer), ledc_channel(ledc_channel) {

    auto defaults = PwmOutput::get_defaults();
    this->properties.insert(defaults.begin(), defaults.end());

    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = ledc_timer,
        .freq_hz = (uint32_t)this->properties.at("frequency")->integer_value,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    ledc_channel_config_t channel_config = {
        .gpio_num = pin,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = ledc_channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = ledc_timer,
        .duty = 0,
        .hpoint = 0,
        .flags = {},
    };

    esp_err_t err = ESP_OK;
    err |= gpio_reset_pin(pin);
    err |= ledc_timer_config(&timer_config);
    err |= gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    err |= ledc_channel_config(&channel_config);
    if (err != ESP_OK) {
        this->set_error(0x01);
    }
}

void PwmOutput::step() {
    esp_err_t err = ESP_OK;
    uint32_t frequency = this->properties.at("frequency")->integer_value;
    err |= ledc_set_freq(LEDC_HIGH_SPEED_MODE, this->ledc_timer, frequency);
    uint32_t duty = this->properties.at("duty")->integer_value;
    err |= ledc_set_duty(LEDC_HIGH_SPEED_MODE, this->ledc_channel, this->is_on ? duty : 0);
    err |= ledc_update_duty(LEDC_HIGH_SPEED_MODE, this->ledc_channel);
    if (err != ESP_OK) {
        this->set_error(0x02);
    }
    Module::step();
}

void PwmOutput::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "on") {
        Module::expect(arguments, 0);
        this->is_on = true;
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->is_on = false;
    } else {
        Module::call(method_name, arguments);
    }
}
