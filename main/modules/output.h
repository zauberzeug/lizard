#pragma once

#include "driver/gpio.h"
#include "mcp23017.h"
#include "module.h"

class Output : public Module {
private:
    int target_level = 0;
    double pulse_interval = 0.0;
    double pulse_duty_cycle = 0.5;
    virtual void set_level(bool level) const = 0;
    bool enabled = true;
    bool active = false;

protected:
    Output(const std::string name);

public:
    void enable();
    void disable();
    void activate();
    void deactivate();
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};

class GpioOutput : public Output {
private:
    const gpio_num_t number;
    void set_level(bool level) const override;

public:
    GpioOutput(const std::string name, const gpio_num_t number);
};

class McpOutput : public Output {
private:
    const Mcp23017_ptr mcp;
    const uint8_t number;
    void set_level(bool level) const override;

public:
    McpOutput(const std::string name, const Mcp23017_ptr mcp, const uint8_t number);
};
