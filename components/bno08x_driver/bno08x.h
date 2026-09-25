#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "sh2.h"
#include "sh2_SensorValue.h"
#include "sh2_err.h"

class I2cDevice;

static constexpr uint8_t I2C_ADDR_DEFAULT = 0x4A;

class Bno08x {
public:
    explicit Bno08x(gpio_num_t reset_pin = GPIO_NUM_NC);
    ~Bno08x();

    bool begin_i2c(i2c_port_t port, uint8_t i2c_addr = I2C_ADDR_DEFAULT,
                   gpio_num_t int_pin = GPIO_NUM_NC, int32_t sensor_id = 0);

    void hardwareReset();
    bool wasReset();

    bool enableReport(sh2_SensorId_t sensor, uint32_t interval_us = 10000);

    /// Pops the next decoded sensor event. Services the SH2 transport when the queue is empty; one SHTP
    /// packet may carry several reports (all sensors due at the same tick are bundled), and every one of
    /// them is queued — the Adafruit driver this is derived from kept only the last report per packet.
    bool getSensorEvent(sh2_SensorValue_t *value);

    /// Drops every queued event, e.g. after a report configuration change so that reports of the old
    /// configuration (decoded while sh2_setSensorConfig waited for its response) are not published.
    void clearSensorEvents();

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
    void pushSensorEvent(const sh2_SensorValue_t &value);
    bool reset_occurred;

private:
    bool popSensorEvent(sh2_SensorValue_t *value);

    // One SHTP cargo carries at most one report per enabled sensor (7 in ndof mode) when the host keeps up;
    // after a host stall the hub may batch a few ticks into one cargo, which is pushed in a single burst.
    static constexpr size_t EVENT_QUEUE_SIZE = 16;
    std::array<sh2_SensorValue_t, EVENT_QUEUE_SIZE> event_queue;
    size_t queue_head = 0;  // next event to pop
    size_t queue_count = 0; // events waiting
    bool overflow_logged = false;
};
