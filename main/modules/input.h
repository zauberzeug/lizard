#pragma once

#include "driver/gpio.h"
#include "mcp23017.h"
#include "module.h"

class Input : public Module {
private:
    virtual bool get_level() const = 0;
    virtual void set_pull_mode(const gpio_pull_mode_t mode) const = 0;

protected:
    Input(const std::string name);

public:
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    std::string get_output() const override;
};

class GpioInput : public Input {
private:
    const gpio_num_t number;
    bool get_level() const override;
    void set_pull_mode(const gpio_pull_mode_t mode) const override;

public:
    GpioInput(const std::string name, const gpio_num_t number);
};

class McpInput : public Input {
private:
    const Mcp23017_ptr mcp;
    const int number;
    bool get_level() const override;
    void set_pull_mode(const gpio_pull_mode_t mode) const override;

public:
    McpInput(const std::string name, const Mcp23017_ptr mcp, const int number);
};