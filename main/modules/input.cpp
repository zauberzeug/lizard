#include "input.h"
#include "../utils/string_utils.h"
#include "../utils/uart.h"
#include <memory>
#include <stdexcept>

REGISTER_MODULE_DEFAULTS(Input)

const std::map<std::string, Variable_ptr> Input::get_defaults() {
    return {
        {"level", std::make_shared<IntegerVariable>()},
        {"change", std::make_shared<IntegerVariable>()},
        {"inverted", std::make_shared<BooleanVariable>()},
        {"active", std::make_shared<BooleanVariable>()},
    };
}

void Input::set_error_descriptions() {
    error_descriptions = {
        {0x01, "Could not initialize input"},
    };
}

Input::Input(const std::string name) : Module(input, name) {
    auto defaults = Input::get_defaults();
    this->properties.insert(defaults.begin(), defaults.end());
}

void Input::step() {
    const int new_level = this->get_level();
    this->properties.at("change")->integer_value = new_level - this->properties.at("level")->integer_value;
    this->properties.at("level")->integer_value = new_level;
    this->properties.at("active")->boolean_value = this->properties.at("inverted")->boolean_value ? !new_level : new_level;
    Module::step();
}

void Input::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "get") {
        Module::expect(arguments, 0);
        echo("%s %d", this->name.c_str(), this->get_level());
    } else if (method_name == "pullup") {
        Module::expect(arguments, 0);
        this->set_pull_mode(GPIO_PULLUP_ONLY);
    } else if (method_name == "pulldown") {
        Module::expect(arguments, 0);
        this->set_pull_mode(GPIO_PULLDOWN_ONLY);
    } else if (method_name == "pulloff") {
        Module::expect(arguments, 0);
        this->set_pull_mode(GPIO_FLOATING);
    } else {
        Module::call(method_name, arguments);
    }
}

std::string Input::get_output() const {
    static char buffer[256];
    csprintf(buffer, sizeof(buffer), "%d", this->get_level());
    return buffer;
}

GpioInput::GpioInput(const std::string name, const gpio_num_t number)
    : Input(name), number(number) {
    esp_err_t err = gpio_reset_pin(number);
    if (err != ESP_OK) {
        this->set_error(0x01);
    }
    err = gpio_set_direction(number, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        this->set_error(0x01);
    }
    this->properties.at("level")->integer_value = this->get_level();
}

bool GpioInput::get_level() const {
    return gpio_get_level(this->number);
}

void GpioInput::set_pull_mode(const gpio_pull_mode_t mode) const {
    gpio_set_pull_mode(this->number, mode);
}

McpInput::McpInput(const std::string name, const Mcp23017_ptr mcp, const uint8_t number)
    : Input(name), mcp(mcp), number(number) {
    this->mcp->set_input(this->number, true);
    this->properties.at("level")->integer_value = this->get_level();
}

bool McpInput::get_level() const {
    return this->mcp->get_level(this->number);
}

void McpInput::set_pull_mode(const gpio_pull_mode_t mode) const {
    if (mode == GPIO_PULLDOWN_ONLY) {
        throw std::runtime_error("pulldown mode is not supported");
    }
    this->mcp->set_pullup(this->number, mode == GPIO_PULLUP_ONLY);
}
