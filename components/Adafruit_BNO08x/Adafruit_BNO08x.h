/*!
 *  @file Adafruit_BNO08x.h
 *
 * 	I2C Driver for the Adafruit BNO08x 9-DOF Orientation IMU Fusion Breakout
 *
 * 	This is a library for the Adafruit BNO08x breakout:
 * 	https://www.adafruit.com/products/4754
 *
 * 	Adafruit invests time and resources providing this open source code,
 *  please support Adafruit and open-source hardware by purchasing products from
 * 	Adafruit!
 *
 *
 *	BSD license (see license.txt)
 */

#ifndef _ADAFRUIT_BNO08x_H
#define _ADAFRUIT_BNO08x_H

#include <cstdint>
#include <memory>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "sh2.h"
#include "sh2_SensorValue.h"
#include "sh2_err.h"

class I2cDevice;

#define BNO08x_I2CADDR_DEFAULT 0x4A ///< The default I2C address

/*!
 *    @brief  Class that stores state and functions for interacting with
 *            the BNO08x 9-DOF Orientation IMU Fusion Breakout
 */
class Adafruit_BNO08x {
public:
    explicit Adafruit_BNO08x(gpio_num_t reset_pin = GPIO_NUM_NC);
    ~Adafruit_BNO08x();

    bool begin_I2C(i2c_port_t port, uint8_t i2c_addr = BNO08x_I2CADDR_DEFAULT, gpio_num_t int_pin = GPIO_NUM_NC,
            int32_t sensor_id = 0);

    void hardwareReset();
    bool wasReset();

    bool enableReport(sh2_SensorId_t sensor, uint32_t interval_us = 10000);
    bool getSensorEvent(sh2_SensorValue_t *value);

    sh2_ProductIds_t prodIds; ///< The product IDs returned by the sensor

protected:
    virtual bool init(int32_t sensor_id);

    i2c_port_t port;
    uint8_t address;
    gpio_num_t int_pin;
    gpio_num_t reset_pin;

    sh2_Hal_t hal; ///< Struct representing the SH2 Hardware Abstraction Layer
    std::unique_ptr<class I2cDevice> device;

public:
    class I2cDevice *get_device() const;
};

#endif
