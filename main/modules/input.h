#pragma once

#include "driver/gpio.h"
#include "mcp23017.h"
#include "module.h"

class Input;
using Input_ptr = std::shared_ptr<Input>;

class Input : public Module {
private:
    virtual void set_pull_mode(const gpio_pull_mode_t mode) const = 0;

protected:
    Input(const std::string name);
    void set_error_descriptions() override;

public:
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
    std::string get_output() const override;
    virtual bool get_level() const = 0;
};

class GpioInput : public Input {
private:
    const gpio_num_t number;
    void set_pull_mode(const gpio_pull_mode_t mode) const override;

public:
    GpioInput(const std::string name, const gpio_num_t number);
    bool get_level() const override;
};

class McpInput : public Input {
private:
    const Mcp23017_ptr mcp;
    const uint8_t number;
    void set_pull_mode(const gpio_pull_mode_t mode) const override;

public:
    McpInput(const std::string name, const Mcp23017_ptr mcp, const uint8_t number);
    bool get_level() const override;
};
