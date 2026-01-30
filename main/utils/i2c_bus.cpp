#include "i2c_bus.h"

#include <stdexcept>

namespace {
constexpr int I2C_MASTER_TX_BUF_DISABLE = 0;
constexpr int I2C_MASTER_RX_BUF_DISABLE = 0;
constexpr uint32_t DEFAULT_TIMEOUT_TICKS = 1048575;
}

std::map<i2c_port_t, I2cBusManager::BusConfig> I2cBusManager::configs;
std::mutex I2cBusManager::configs_mutex;

void I2cBusManager::ensure(i2c_port_t port, gpio_num_t sda_pin, gpio_num_t scl_pin, int clk_speed_hz) {
    std::lock_guard<std::mutex> lock(configs_mutex);

    BusConfig &config = configs[port];
    if (config.initialized) {
        if (config.sda_pin != sda_pin || config.scl_pin != scl_pin || config.clk_speed_hz != clk_speed_hz) {
            throw std::runtime_error("i2c bus already initialized with different parameters");
        }
        return;
    }

    i2c_config_t i2c_config = {};
    i2c_config.mode = I2C_MODE_MASTER;
    i2c_config.sda_io_num = sda_pin;
    i2c_config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_config.scl_io_num = scl_pin;
    i2c_config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_config.master.clk_speed = clk_speed_hz;
    i2c_config.clk_flags = 0;

    if (i2c_param_config(port, &i2c_config) != ESP_OK) {
        throw std::runtime_error("could not configure i2c port");
    }
    if (i2c_driver_install(port, I2C_MODE_MASTER, I2C_MASTER_TX_BUF_DISABLE, I2C_MASTER_RX_BUF_DISABLE, 0) != ESP_OK) {
        throw std::runtime_error("could not install i2c driver");
    }
    if (i2c_set_timeout(port, DEFAULT_TIMEOUT_TICKS) != ESP_OK) {
        throw std::runtime_error("could not set i2c timeout");
    }

    config = BusConfig{.sda_pin = sda_pin, .scl_pin = scl_pin, .clk_speed_hz = clk_speed_hz, .initialized = true};
}

