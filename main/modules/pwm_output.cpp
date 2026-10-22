#include "pwm_output.h"
#include "../global.h"
#include "../utils/esp_error.h"
#include <driver/ledc.h>
#include <soc/soc.h>

#ifdef CONFIG_IDF_TARGET_ESP32S3
#define SPEED_MODE LEDC_LOW_SPEED_MODE
#else
#define SPEED_MODE LEDC_HIGH_SPEED_MODE
#endif

static constexpr ledc_timer_bit_t DUTY_RESOLUTION = LEDC_TIMER_8_BIT;
static constexpr int64_t MAX_DUTY = 1 << DUTY_RESOLUTION;                       // 256 steps per period, see the duty table in the docs
static constexpr int64_t MAX_FREQUENCY = APB_CLK_FREQ / (1 << DUTY_RESOLUTION); // 312500 Hz, the fastest clock divided by one period of duty steps

static Module_ptr create_pwm_output(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler) {
    if (arguments.size() < 1 || arguments.size() > 3) {
        throw std::runtime_error("unexpected number of arguments");
    }
    Module::expect(arguments, -1, integer, integer, integer);
    const gpio_num_t pin = (gpio_num_t)arguments[0]->evaluate_integer();
    const int64_t ledc_timer = arguments.size() > 1 ? arguments[1]->evaluate_integer() : 0;
    const int64_t ledc_channel = arguments.size() > 2 ? arguments[2]->evaluate_integer() : 0;
    if (ledc_timer < 0 || ledc_timer >= LEDC_TIMER_MAX) {
        throw std::runtime_error("LEDC timer must be 0.." + std::to_string(LEDC_TIMER_MAX - 1));
    }
    if (ledc_channel < 0 || ledc_channel >= LEDC_CHANNEL_MAX) {
        throw std::runtime_error("LEDC channel must be 0.." + std::to_string(LEDC_CHANNEL_MAX - 1));
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

    // Outputs on one timer share its frequency: follow the existing ones instead of resetting the timer to the default.
    const std::vector<PwmOutput *> siblings = this->timer_siblings();
    if (siblings.empty()) {
        this->configure_timer(this->properties.at("frequency")->integer_value());
    } else {
        this->properties.at("frequency")->set_integer_value(siblings.front()->properties.at("frequency")->integer_value());
    }
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
    check_error(ledc_channel_config(&channel_config), "could not configure LEDC channel");
}

void PwmOutput::configure_timer(const uint32_t frequency) {
    const ledc_timer_config_t timer_config = {
        .speed_mode = SPEED_MODE,
        .duty_resolution = DUTY_RESOLUTION,
        .timer_num = this->ledc_timer,
        .freq_hz = frequency,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    check_error(ledc_timer_config(&timer_config), "could not configure LEDC timer for " + std::to_string(frequency) + " Hz");
}

std::vector<PwmOutput *> PwmOutput::timer_siblings() const {
    std::vector<PwmOutput *> siblings;
    for (auto const &[name, module] : Global::modules) {
        PwmOutput *const other = dynamic_cast<PwmOutput *>(module.get());
        if (other != nullptr && other != this && other->ledc_timer == this->ledc_timer) {
            siblings.push_back(other);
        }
    }
    return siblings;
}

void PwmOutput::write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) {
    if (expression->type == integer) {
        const int64_t value = expression->evaluate_integer();
        if (property_name == "frequency") {
            if (value < 1 || value > MAX_FREQUENCY) {
                throw std::runtime_error("frequency must be 1.." + std::to_string(MAX_FREQUENCY) + " Hz");
            }
            if (value != this->properties.at("frequency")->integer_value()) {
                this->configure_timer(value);
                for (PwmOutput *const sibling : this->timer_siblings()) {
                    sibling->properties.at("frequency")->set_integer_value(value);
                }
            }
        } else if (property_name == "duty") {
            if (value < 0 || value > MAX_DUTY) {
                throw std::runtime_error("duty must be 0.." + std::to_string(MAX_DUTY));
            }
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
