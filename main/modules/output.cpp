#include "output.h"
#include "utils/timing.h"
#include <math.h>
#include <stdexcept>

REGISTER_MODULE_DEFAULTS(Output)

const std::map<std::string, Variable_ptr> Output::get_defaults() {
    return {
        {"level", std::make_shared<IntegerVariable>()},
        {"change", std::make_shared<IntegerVariable>()},
        {"inverted", std::make_shared<BooleanVariable>(false)},
        {"active", std::make_shared<BooleanVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
    };
}

Output::Output(const std::string name) : Module(output, name) {
    this->properties = Output::get_defaults();
}

void Output::step() {
    if (this->pulse_interval > 0) {
        this->target_level = fmod(millis() / 1000.0, this->pulse_interval) / this->pulse_interval < this->pulse_duty_cycle;
    }

    this->set_level(this->target_level);
    this->properties.at("change")->integer_value = this->target_level - this->properties.at("level")->integer_value;
    this->properties.at("level")->integer_value = this->target_level;
    if (this->properties.at("enabled")->boolean_value != this->enabled) {
        if (this->properties.at("enabled")->boolean_value) {
            this->enable();
        } else {
            this->disable();
        }
    }
    if (this->properties.at("active")->boolean_value != this->active) {
        if (this->properties.at("active")->boolean_value) {
            this->activate();
        } else {
            this->deactivate();
        }
    }
}

void Output::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "on") {
        Module::expect(arguments, 0);
        if (this->enabled) {
            this->target_level = 1;
            this->pulse_interval = 0;
            this->step();
        }
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        if (this->enabled) {
            this->target_level = 0;
            this->pulse_interval = 0;
            this->step();
        }
    } else if (method_name == "level") {
        Module::expect(arguments, 1, boolean);
        if (this->enabled) {
            this->target_level = arguments[0]->evaluate_boolean();
            this->pulse_interval = 0;
            this->step();
        }
    } else if (method_name == "pulse") {
        if (arguments.size() < 1 || arguments.size() > 2) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, numbery, numbery);
        if (this->enabled) {
            this->pulse_interval = arguments[0]->evaluate_number();
            this->pulse_duty_cycle = arguments.size() > 1 ? arguments[1]->evaluate_number() : 0.5;
        }
    } else if (method_name == "enable") {
        Module::expect(arguments, 0);
        this->enable();
    } else if (method_name == "disable") {
        Module::expect(arguments, 0);
        this->disable();
    } else if (method_name == "activate") {
        Module::expect(arguments, 0);
        this->activate();
    } else if (method_name == "deactivate") {
        Module::expect(arguments, 0);
        this->deactivate();
    } else {
        Module::call(method_name, arguments);
    }
}

GpioOutput::GpioOutput(const std::string name, const gpio_num_t number)
    : Output(name), number(number) {
    gpio_reset_pin(number);
    gpio_set_direction(number, GPIO_MODE_OUTPUT);
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

void Output::enable() {
    this->enabled = true;
    this->properties.at("enabled")->boolean_value = true;
}

void Output::disable() {
    this->deactivate();
    this->enabled = false;
    this->properties.at("enabled")->boolean_value = false;
}

void Output::activate() {
    if (this->enabled) {
        this->active = true;
        this->properties.at("active")->boolean_value = true;
        this->target_level = this->properties.at("inverted")->boolean_value ? 0 : 1;
        this->pulse_interval = 0;
        this->step();
    }
}

void Output::deactivate() {
    if (this->enabled) {
        this->active = false;
        this->properties.at("active")->boolean_value = false;
        this->target_level = this->properties.at("inverted")->boolean_value ? 1 : 0;
        this->pulse_interval = 0;
        this->step();
    }
}
