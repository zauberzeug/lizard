#pragma once

#include "Adafruit_BNO08x.h"
#include "driver/i2c.h"
#include "module.h"
#include <memory>

class ImuBno085;
using ImuBno085_ptr = std::shared_ptr<ImuBno085>;

class ImuBno085 : public Module {
private:
    i2c_port_t i2c_port;
    gpio_num_t sda_pin;
    gpio_num_t scl_pin;
    gpio_num_t int_pin;
    gpio_num_t rst_pin;
    uint8_t address;
    int clk_speed;
    uint32_t report_interval_us;
    std::unique_ptr<Adafruit_BNO08x> bno;

public:
    ImuBno085(const std::string name, i2c_port_t i2c_port, gpio_num_t sda_pin, gpio_num_t scl_pin, gpio_num_t int_pin,
            gpio_num_t rst_pin, uint8_t address, int clk_speed);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
