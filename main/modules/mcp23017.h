#pragma once

#include "driver/i2c.h"
#include "module.h"

typedef enum {
    MCP23017_REG_IODIRA = 0x00,
    MCP23017_REG_IODIRB = 0x01,
    MCP23017_REG_GPPUA = 0x0c,
    MCP23017_REG_GPPUB = 0x0d,
    MCP23017_REG_GPIOA = 0x12,
    MCP23017_REG_GPIOB = 0x13,
} mcp23017_reg_t;

class Mcp23017;
using Mcp23017_ptr = std::shared_ptr<Mcp23017>;

class Mcp23017 : public Module {
private:
    const i2c_port_t i2c_port;
    const uint8_t address;

    void write_register(mcp23017_reg_t reg, uint8_t value) const;
    uint8_t read_register(mcp23017_reg_t reg) const;
    uint16_t read_pins() const;
    void write_pins(uint16_t value) const;
    void set_inputs(uint16_t inputs) const;
    void set_pullups(uint16_t pullups) const;

public:
    Mcp23017(const std::string name, i2c_port_t i2c_port, gpio_num_t sda_pin, gpio_num_t scl_pin, uint8_t address, int clk_speed);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;

    bool get_level(const uint8_t number) const;
    void set_input(const uint8_t number, const bool value) const;
    void set_pullup(const uint8_t number, const bool value) const;
};
