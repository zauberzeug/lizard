#include "mcp23017.h"

#define I2C_MASTER_TX_BUF_DISABLE 0
#define I2C_MASTER_RX_BUF_DISABLE 0

Mcp23017::Mcp23017(const std::string name, i2c_port_t i2c_port, gpio_num_t sda_pin, gpio_num_t scl_pin, uint8_t address, int clk_speed)
    : Module(mcp23017, name), i2c_port(i2c_port), address(address) {
    i2c_config_t config;
    config.mode = I2C_MODE_MASTER;
    config.sda_io_num = sda_pin,
    config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    config.scl_io_num = scl_pin;
    config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    config.master.clk_speed = clk_speed;
    config.clk_flags = 0;
    if (i2c_param_config(i2c_port, &config) != ESP_OK) {
        throw std::runtime_error("could not configure i2c port");
    }
    if (i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_TX_BUF_DISABLE, I2C_MASTER_RX_BUF_DISABLE, 0) != ESP_OK) {
        throw std::runtime_error("could not install i2c driver");
    }

    this->properties["levels"] = std::make_shared<IntegerVariable>();
    this->properties["inputs"] = std::make_shared<IntegerVariable>(0xffff); // default: all pins input
    this->properties["pullups"] = std::make_shared<IntegerVariable>();

    this->set_inputs(this->properties.at("inputs")->integer_value);
    this->set_pullups(this->properties.at("pullups")->integer_value);
}

void Mcp23017::step() {
    this->properties.at("levels")->integer_value = this->read_pins();
    Module::step();
}

void Mcp23017::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "levels") {
        Module::expect(arguments, 1, integer);
        const uint16_t value = arguments[0]->evaluate_integer();
        this->properties.at("levels")->integer_value = value;
        this->write_pins(value);
    } else if (method_name == "pullups") {
        Module::expect(arguments, 1, integer);
        const uint16_t value = arguments[0]->evaluate_integer();
        this->properties.at("pullups")->integer_value = value;
        this->set_pullups(value);
    } else if (method_name == "inputs") {
        Module::expect(arguments, 1, integer);
        const uint16_t value = arguments[0]->evaluate_integer();
        this->properties.at("inputs")->integer_value = value;
        this->set_inputs(value);
    } else {
        Module::call(method_name, arguments);
    }
}

void Mcp23017::write_register(mcp23017_reg_t reg, uint8_t value) const {
    i2c_cmd_handle_t command = i2c_cmd_link_create();
    i2c_master_start(command);
    i2c_master_write_byte(command, (this->address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(command, reg, true);
    i2c_master_write_byte(command, value, true);
    i2c_master_stop(command);
    if (i2c_master_cmd_begin(this->i2c_port, command, 1000 / portTICK_RATE_MS) != ESP_OK) {
        i2c_cmd_link_delete(command);
        throw std::runtime_error("unable to send i2c command");
    }
    i2c_cmd_link_delete(command);
}

uint8_t Mcp23017::read_register(mcp23017_reg_t reg) const {
    i2c_cmd_handle_t command = i2c_cmd_link_create();
    i2c_master_start(command);
    i2c_master_write_byte(command, (this->address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(command, (uint8_t)reg, true);
    i2c_master_stop(command);
    if (i2c_master_cmd_begin(this->i2c_port, command, 1000 / portTICK_RATE_MS) != ESP_OK) {
        i2c_cmd_link_delete(command);
        throw std::runtime_error("unable to prepare mcp23017 to be read");
    }
    i2c_cmd_link_delete(command);
    uint8_t value;
    command = i2c_cmd_link_create();
    i2c_master_start(command);
    i2c_master_write_byte(command, (this->address << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(command, &value, I2C_MASTER_NACK);
    i2c_master_stop(command);
    if (i2c_master_cmd_begin(this->i2c_port, command, 1000 / portTICK_RATE_MS) != ESP_OK) {
        i2c_cmd_link_delete(command);
        throw std::runtime_error("unable to read data from mcp23017");
    }
    i2c_cmd_link_delete(command);
    return value;
}

uint16_t Mcp23017::read_pins() const {
    uint8_t a = this->read_register(MCP23017_REG_GPIOA);
    uint8_t b = this->read_register(MCP23017_REG_GPIOB);
    return (b << 8) | a;
}

void Mcp23017::write_pins(uint16_t value) const {
    this->write_register(MCP23017_REG_GPIOA, value);
    this->write_register(MCP23017_REG_GPIOB, value >> 8);
}

void Mcp23017::set_inputs(uint16_t inputs) const {
    this->write_register(MCP23017_REG_IODIRA, inputs);
    this->write_register(MCP23017_REG_IODIRB, inputs >> 8);
}

void Mcp23017::set_pullups(uint16_t pullups) const {
    this->write_register(MCP23017_REG_GPPUA, pullups);
    this->write_register(MCP23017_REG_GPPUB, pullups >> 8);
}
