#pragma once

#include "BNO055ESP32.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "module.h"
#include <atomic>

class Imu;
using Imu_ptr = std::shared_ptr<Imu>;

using Bno_ptr = std::shared_ptr<BNO055>;

class Imu : public Module {
public:
    static inline constexpr const char *TYPE = "Imu";

    Imu(const std::string name, i2c_port_t i2c_port, gpio_num_t sda_pin, gpio_num_t scl_pin, uint8_t address, int clk_speed);
    ~Imu() override;
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

private:
    struct Sample {
        uint16_t data_select;
        bno055_calibration_t calibration;
        bno055_vector_t accelerometer;
        bno055_vector_t magnetometer;
        bno055_vector_t gyroscope;
        bno055_vector_t euler;
        bno055_quaternion_t quaternion;
        bno055_vector_t linear_acceleration;
        bno055_vector_t gravity;
        int8_t temperature;
    };

    static void read_loop(void *imu);
    Sample read_sample(uint16_t data_select) const;
    void publish(const Sample &sample);
    void delete_task_resources();

    const i2c_port_t i2c_port;
    const uint8_t address;
    Bno_ptr bno;
    SemaphoreHandle_t bno_mutex{};
    QueueHandle_t sample_queue{};
    TaskHandle_t read_task{};
    SemaphoreHandle_t read_task_stopped{};
    std::atomic<bool> stop_requested{false};
    std::atomic<uint16_t> requested_data_select{0xffff};
    std::atomic<bool> read_failed{false};
    std::atomic<uint32_t> failed_reads{0};
    uint32_t reported_failed_reads = 0;
    bool read_failure_reported = false;
};
