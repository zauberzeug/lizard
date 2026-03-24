#pragma once

#include <cstdint>
#include <memory>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "sh2.h"
#include "sh2_SensorValue.h"
#include "sh2_err.h"

class I2cDevice;

static constexpr uint8_t kI2cAddrDefault = 0x4A;

class Bno08x {
public:
    explicit Bno08x(gpio_num_t reset_pin = GPIO_NUM_NC);
    ~Bno08x();

    bool begin_i2c(i2c_port_t port, uint8_t i2c_addr = kI2cAddrDefault,
                   gpio_num_t int_pin = GPIO_NUM_NC, int32_t sensor_id = 0);

    void hardwareReset();
    bool wasReset();

    bool enableReport(sh2_SensorId_t sensor, uint32_t interval_us = 10000);
    bool getSensorEvent(sh2_SensorValue_t *value);

    I2cDevice *get_device() const;

private:
    sh2_ProductIds_t prodIds;
    bool init(int32_t sensor_id);

    i2c_port_t port;
    uint8_t address;
    gpio_num_t int_pin;
    gpio_num_t reset_pin;

    sh2_Hal_t hal;
    std::unique_ptr<I2cDevice> device;

public:
    // accessed by SH2 HAL callbacks
    sh2_SensorValue_t *pending_value;
    bool reset_occurred;
};
