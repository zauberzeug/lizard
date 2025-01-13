#include "output.h"
#include "utils/timing.h"
#include <math.h>
#include <stdexcept>

const std::map<std::string, Variable_ptr> Output::get_defaults() {
    return {
        {"level", std::make_shared<IntegerVariable>()},
        {"change", std::make_shared<IntegerVariable>()},
    };
}

void Output::set_error_descriptions() {
    this->error_descriptions = {
        {0x01, "Could not configure GPIO"},
    };
}

Output::Output(const std::string name) : Module(output, name) {
    auto defaults = Output::get_defaults();
    this->properties.insert(defaults.begin(), defaults.end());
}

void Output::step() {
    if (this->pulse_interval > 0) {
        this->target_level = fmod(millis() / 1000.0, this->pulse_interval) / this->pulse_interval < this->pulse_duty_cycle;
    }

    this->set_level(this->target_level);
    this->properties.at("change")->integer_value = this->target_level - this->properties.at("level")->integer_value;
    this->properties.at("level")->integer_value = this->target_level;
}

void Output::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "on") {
        Module::expect(arguments, 0);
        this->target_level = 1;
        this->pulse_interval = 0;
        this->step();
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->target_level = 0;
        this->pulse_interval = 0;
        this->step();
    } else if (method_name == "level") {
        Module::expect(arguments, 1, boolean);
        this->target_level = arguments[0]->evaluate_boolean();
        this->pulse_interval = 0;
        this->step();
    } else if (method_name == "pulse") {
        if (arguments.size() < 1 || arguments.size() > 2) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, numbery, numbery);
        this->pulse_interval = arguments[0]->evaluate_number();
        this->pulse_duty_cycle = arguments.size() > 1 ? arguments[1]->evaluate_number() : 0.5;
    } else {
        Module::call(method_name, arguments);
    }
}

GpioOutput::GpioOutput(const std::string name, const gpio_num_t number)
    : Output(name), number(number) {
    esp_err_t err = gpio_reset_pin(number);
    if (err != ESP_OK) {
        this->set_error(0x01);
    }
    err = gpio_set_direction(number, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        this->set_error(0x01);
    }
}

void GpioOutput::set_level(bool level) const {
    gpio_set_level(this->number, level);
}

McpOutput::McpOutput(const std::string name, const Mcp23017_ptr mcp, const uint8_t number)
    : Output(name), mcp(mcp), number(number) {
    this->mcp->set_input(this->number, false);
}

void McpOutput::set_level(bool level) const {
    this->mcp->set_level(this->number, level);
}
