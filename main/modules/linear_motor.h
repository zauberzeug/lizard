#pragma once

#include "driver/gpio.h"
#include "mcp23017.h"
#include "module.h"

class LinearMotor : public Module {
private:
    virtual bool get_in() const = 0;
    virtual bool get_out() const = 0;
    virtual void set_in(bool level) const = 0;
    virtual void set_out(bool level) const = 0;

protected:
    LinearMotor(const std::string name);

public:
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
    void set_error_descriptions();
};

class GpioLinearMotor : public LinearMotor {
private:
    const gpio_num_t move_in;
    const gpio_num_t move_out;
    const gpio_num_t end_in;
    const gpio_num_t end_out;
    bool get_in() const override;
    bool get_out() const override;
    void set_in(bool level) const override;
    void set_out(bool level) const override;

public:
    GpioLinearMotor(const std::string name,
                    const gpio_num_t move_in,
                    const gpio_num_t move_out,
                    const gpio_num_t end_in,
                    const gpio_num_t end_out);
};

class McpLinearMotor : public LinearMotor {
private:
    const Mcp23017_ptr mcp;
    const uint8_t move_in;
    const uint8_t move_out;
    const uint8_t end_in;
    const uint8_t end_out;
    bool get_in() const override;
    bool get_out() const override;
    void set_in(bool level) const override;
    void set_out(bool level) const override;

public:
    McpLinearMotor(const std::string name,
                   const Mcp23017_ptr mcp,
                   const uint8_t move_in,
                   const uint8_t move_out,
                   const uint8_t end_in,
                   const uint8_t end_out);
};
