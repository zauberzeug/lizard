#include "linear_motor.h"
#include <memory>

const std::map<std::string, Variable_ptr> LinearMotor::get_defaults() {
    return {
        {"in", std::make_shared<BooleanVariable>()},
        {"out", std::make_shared<BooleanVariable>()},
    };
}

void LinearMotor::set_error_descriptions() {
    this->error_descriptions = {
        {0x01, "GPIO Linear Motor: Could not initialize GPIO pins"},
        {0x02, "MCP Linear Motor: Could not initialize MCP"},
    };
}

LinearMotor::LinearMotor(const std::string name) : Module(output, name) {
    this->set_error_descriptions();
    auto defaults = LinearMotor::get_defaults();
    this->properties.insert(defaults.begin(), defaults.end());
}

void LinearMotor::step() {
    this->properties.at("in")->boolean_value = this->get_in();
    this->properties.at("out")->boolean_value = this->get_out();
    Module::step();
}

void LinearMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "in") {
        Module::expect(arguments, 0);
        this->set_in(1);
        this->set_out(0);
    } else if (method_name == "out") {
        Module::expect(arguments, 0);
        this->set_in(0);
        this->set_out(1);
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->set_in(0);
        this->set_out(0);
    } else {
        Module::call(method_name, arguments);
    }
}

GpioLinearMotor::GpioLinearMotor(const std::string name,
                                 const gpio_num_t move_in,
                                 const gpio_num_t move_out,
                                 const gpio_num_t end_in,
                                 const gpio_num_t end_out)
    : LinearMotor(name), move_in(move_in), move_out(move_out), end_in(end_in), end_out(end_out) {

    esp_err_t err = ESP_OK;
    err |= gpio_reset_pin(move_in);
    err |= gpio_reset_pin(move_out);
    err |= gpio_reset_pin(end_in);
    err |= gpio_reset_pin(end_out);
    err |= gpio_set_direction(move_in, GPIO_MODE_OUTPUT);
    err |= gpio_set_direction(move_out, GPIO_MODE_OUTPUT);
    err |= gpio_set_direction(end_in, GPIO_MODE_INPUT);
    err |= gpio_set_direction(end_out, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        this->set_error(0x01);
    }
    this->properties.at("in")->boolean_value = this->get_in();
    this->properties.at("out")->boolean_value = this->get_out();
}

bool GpioLinearMotor::get_in() const {
    return gpio_get_level(this->end_in) == 1;
}

bool GpioLinearMotor::get_out() const {
    return gpio_get_level(this->end_out) == 1;
}

void GpioLinearMotor::set_in(bool level) const {
    gpio_set_level(this->move_in, level);
}

void GpioLinearMotor::set_out(bool level) const {
    gpio_set_level(this->move_out, level);
}

McpLinearMotor::McpLinearMotor(const std::string name,
                               const Mcp23017_ptr mcp,
                               const uint8_t move_in,
                               const uint8_t move_out,
                               const uint8_t end_in,
                               const uint8_t end_out)
    : LinearMotor(name), mcp(mcp), move_in(move_in), move_out(move_out), end_in(end_in), end_out(end_out) {
    this->mcp->set_input(this->move_in, false);
    this->mcp->set_input(this->move_out, false);
    this->mcp->set_input(this->end_in, true);
    this->mcp->set_input(this->end_out, true);
    this->properties.at("in")->boolean_value = this->get_in();
    this->properties.at("out")->boolean_value = this->get_out();
}

bool McpLinearMotor::get_in() const {
    return this->mcp->get_level(this->end_in) == 1;
}

bool McpLinearMotor::get_out() const {
    return this->mcp->get_level(this->end_out) == 1;
}

void McpLinearMotor::set_in(bool level) const {
    return this->mcp->set_level(this->move_in, level);
}

void McpLinearMotor::set_out(bool level) const {
    return this->mcp->set_level(this->move_out, level);
}
