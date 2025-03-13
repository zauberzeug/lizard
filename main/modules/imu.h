#pragma once

#include "BNO055ESP32.h"
#include "driver/i2c.h"
#include "module.h"

class Imu;
using Imu_ptr = std::shared_ptr<Imu>;

using Bno_ptr = std::shared_ptr<BNO055>;

class Imu : public Module {
private:
    const i2c_port_t i2c_port;
    const uint8_t address;
    Bno_ptr bno;

public:
    Imu(const std::string name, i2c_port_t i2c_port, gpio_num_t sda_pin, gpio_num_t scl_pin, uint8_t address, int clk_speed);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
