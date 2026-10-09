#pragma once

#include <map>
#include <memory>

#include "driver/i2c.h"
#include "resources.h"

class I2cBusManager {
public:
    static void ensure(i2c_port_t port, gpio_num_t sda_pin, gpio_num_t scl_pin, int clk_speed_hz);

private:
    struct BusConfig {
        gpio_num_t sda_pin;
        gpio_num_t scl_pin;
        int clk_speed_hz;
        bool initialized;
        std::unique_ptr<resources::Claims> claims; // the driver is never deinstalled, so the pins stay with the port
    };

    static std::map<i2c_port_t, BusConfig> configs;
};
