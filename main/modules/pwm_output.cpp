#include "pwm_output.h"
#include <driver/ledc.h>

#ifdef CONFIG_IDF_TARGET_ESP32S3
#define SPEED_MODE LEDC_LOW_SPEED_MODE
#else
#define SPEED_MODE LEDC_HIGH_SPEED_MODE
#endif

static Module_ptr create_pwm_output(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler) {
    if (arguments.size() < 1 || arguments.size() > 3) {
        throw std::runtime_error("unexpected number of arguments");
    }
    Module::expect(arguments, -1, integer, integer, integer);
    const gpio_num_t pin = (gpio_num_t)arguments[0]->evaluate_integer();
    const int64_t ledc_timer = arguments.size() > 1 ? arguments[1]->evaluate_integer() : 0;
    const int64_t ledc_channel = arguments.size() > 2 ? arguments[2]->evaluate_integer() : 0;
    if (ledc_timer < 0 || ledc_timer > 3) {
        throw std::runtime_error("LEDC timer must be between 0 and 3");
    }
    if (ledc_channel < 0 || ledc_channel > 7) {
        throw std::runtime_error("LEDC channel must be between 0 and 7");
    }
    return std::make_shared<PwmOutput>(name, pin, (ledc_timer_t)ledc_timer, (ledc_channel_t)ledc_channel);
}
REGISTER_MODULE(PwmOutput, &create_pwm_output)

const std::map<std::string, Variable_ptr> PwmOutput::get_defaults() {
    return {
        {"frequency", std::make_shared<IntegerVariable>(1000)},
        {"duty", std::make_shared<IntegerVariable>(128)},
        {"enabled", std::make_shared<BooleanVariable>(true)},
    };
}

PwmOutput::PwmOutput(const std::string name,
                     const gpio_num_t pin,
                     const ledc_timer_t ledc_timer,
                     const ledc_channel_t ledc_channel)
    : Module(name), pin(pin), ledc_timer(ledc_timer), ledc_channel(ledc_channel) {
    gpio_reset_pin(pin);

    this->properties = PwmOutput::get_defaults();

    this->configure_timer(this->properties.at("frequency")->integer_value());
    ledc_channel_config_t channel_config = {
        .gpio_num = pin,
        .speed_mode = SPEED_MODE,
        .channel = ledc_channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = ledc_timer,
        .duty = 0,
        .hpoint = 0,
        .flags = {},
    };
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    if (ledc_channel_config(&channel_config) != ESP_OK) {
        throw std::runtime_error("could not configure LEDC channel");
    }
}

void PwmOutput::configure_timer(const uint32_t frequency) {
    const ledc_timer_config_t timer_config = {
        .speed_mode = SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = this->ledc_timer,
        .freq_hz = frequency,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    if (ledc_timer_config(&timer_config) != ESP_OK) {
        throw std::runtime_error("could not configure LEDC timer for " + std::to_string(frequency) + " Hz");
    }
}

void PwmOutput::write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) {
    if (property_name == "frequency" && expression->type == integer) {
        const int64_t frequency = expression->evaluate_integer();
        if (frequency < 1 || frequency > 312500) { // 80 MHz / 256 duty steps
            throw std::runtime_error("frequency must be between 1 and 312500 Hz");
        }
        if (frequency != this->properties.at("frequency")->integer_value()) {
            this->configure_timer(frequency);
        }
    }
    if (property_name == "duty" && expression->type == integer) {
        const int64_t duty = expression->evaluate_integer();
        if (duty < 0 || duty > 256) {
            throw std::runtime_error("duty must be between 0 and 256");
        }
    }
    Module::write_property(property_name, expression, from_expander);
}

void PwmOutput::step() {
    uint32_t duty = this->properties.at("duty")->integer_value();
    ledc_set_duty(SPEED_MODE, this->ledc_channel, this->is_on ? duty : 0);
    ledc_update_duty(SPEED_MODE, this->ledc_channel);
    this->sync_enabled();
    Module::step();
}

void PwmOutput::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "on") {
        Module::expect(arguments, 0);
        if (this->enabled) {
            this->is_on = true;
        }
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        if (this->enabled) {
            this->is_on = false;
        }
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

void PwmOutput::do_disable() {
    this->is_on = false;
}
