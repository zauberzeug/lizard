#pragma once

#include <map>
#include <mutex>

#include "driver/i2c.h"

class I2cBusManager {
public:
    static void ensure(i2c_port_t port, gpio_num_t sda_pin, gpio_num_t scl_pin, int clk_speed_hz);

private:
    struct BusConfig {
        gpio_num_t sda_pin;
        gpio_num_t scl_pin;
        int clk_speed_hz;
        bool initialized;
    };

    static std::map<i2c_port_t, BusConfig> configs;
    static std::mutex configs_mutex;
};

