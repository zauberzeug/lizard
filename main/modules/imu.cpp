#include "imu.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include "module_helpers.h"
#include "mutex_guard.h"
#include "uart.h"
#include <algorithm>
#include <stdexcept>

static constexpr uint32_t READ_TASK_STACK_SIZE = 4096;
static constexpr UBaseType_t READ_TASK_PRIORITY = 5;
static constexpr BaseType_t READ_TASK_CORE = 1;
static constexpr TickType_t READ_PERIOD = pdMS_TO_TICKS(10); // the BNO055's fusion rate; a slower read just free-runs
static constexpr TickType_t RETRY_DELAY = pdMS_TO_TICKS(1000);

static Module_ptr create_imu(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler) {
    if (arguments.size() > 5) {
        throw std::runtime_error("unexpected number of arguments");
    }
    Module::expect(arguments, -1, integer, integer, integer, integer, integer);
    const i2c_port_t port = arguments.size() > 0 ? (i2c_port_t)arguments[0]->evaluate_integer() : I2C_NUM_0;
    const gpio_num_t sda_pin = arguments.size() > 1 ? (gpio_num_t)arguments[1]->evaluate_integer() : DEFAULT_I2C_SDA_PIN;
    const gpio_num_t scl_pin = arguments.size() > 2 ? (gpio_num_t)arguments[2]->evaluate_integer() : DEFAULT_I2C_SCL_PIN;
    const uint8_t address = arguments.size() > 3 ? arguments[3]->evaluate_integer() : 0x28;
    const int clk_speed = arguments.size() > 4 ? arguments[4]->evaluate_integer() : 100000;
    return std::make_shared<Imu>(name, port, sda_pin, scl_pin, address, clk_speed);
}
REGISTER_MODULE(Imu, &create_imu)

const std::map<std::string, Variable_ptr> Imu::get_defaults() {
    return {
        {"cal_sys", std::make_shared<IntegerVariable>()},
        {"cal_gyr", std::make_shared<IntegerVariable>()},
        {"cal_acc", std::make_shared<IntegerVariable>()},
        {"cal_mag", std::make_shared<IntegerVariable>()},
        {"acc_x", std::make_shared<NumberVariable>()},
        {"acc_y", std::make_shared<NumberVariable>()},
        {"acc_z", std::make_shared<NumberVariable>()},
        {"mag_x", std::make_shared<NumberVariable>()},
        {"mag_y", std::make_shared<NumberVariable>()},
        {"mag_z", std::make_shared<NumberVariable>()},
        {"gyr_x", std::make_shared<NumberVariable>()},
        {"gyr_y", std::make_shared<NumberVariable>()},
        {"gyr_z", std::make_shared<NumberVariable>()},
        {"yaw", std::make_shared<NumberVariable>()},
        {"roll", std::make_shared<NumberVariable>()},
        {"pitch", std::make_shared<NumberVariable>()},
        {"quat_w", std::make_shared<NumberVariable>()},
        {"quat_x", std::make_shared<NumberVariable>()},
        {"quat_y", std::make_shared<NumberVariable>()},
        {"quat_z", std::make_shared<NumberVariable>()},
        {"lin_x", std::make_shared<NumberVariable>()},
        {"lin_y", std::make_shared<NumberVariable>()},
        {"lin_z", std::make_shared<NumberVariable>()},
        {"grav_x", std::make_shared<NumberVariable>()},
        {"grav_y", std::make_shared<NumberVariable>()},
        {"grav_z", std::make_shared<NumberVariable>()},
        {"temp", std::make_shared<IntegerVariable>()},
        {"data_select", std::make_shared<IntegerVariable>(0xffff)},
    };
}

Imu::Imu(const std::string name, i2c_port_t i2c_port, gpio_num_t sda_pin, gpio_num_t scl_pin, uint8_t address, int clk_speed)
    : Module(name), i2c_port(i2c_port), address(address) {
    // The driver logs every failed retry round (up to 64 per transaction) straight to UART0, without checksum and
    // from the read task, flooding the console on a bad bus. Failures are reported through step() instead.
    esp_log_level_set("BNO055", ESP_LOG_NONE);
    I2cBusManager::ensure(i2c_port, sda_pin, scl_pin, clk_speed);
    this->bno = std::make_shared<BNO055>((i2c_port_t)i2c_port, address);
    try {
        this->bno->begin();
        this->bno->enableExternalCrystal();
        this->bno->setOprModeNdof();
    } catch (std::exception &ex) {
        throw std::runtime_error(std::string("imu setup failed: ") + ex.what());
    }
    this->properties = Imu::get_defaults();

    this->bno_mutex = xSemaphoreCreateMutex();
    this->sample_queue = xQueueCreate(1, sizeof(Sample));
    this->read_task_stopped = xSemaphoreCreateBinary();
    if (!this->bno_mutex || !this->sample_queue || !this->read_task_stopped ||
        xTaskCreatePinnedToCore(Imu::read_loop, "imu_read", READ_TASK_STACK_SIZE, this,
                                READ_TASK_PRIORITY, &this->read_task, READ_TASK_CORE) != pdPASS) {
        this->delete_task_resources();
        throw std::runtime_error("imu setup failed: could not start the read task");
    }
}

Imu::~Imu() {
    // Stop the task cooperatively: deleting it from outside could catch it holding the bus or the heap lock.
    this->stop_requested.store(true);
    xTaskNotifyGive(this->read_task); // cut a retry delay short
    xSemaphoreTake(this->read_task_stopped, portMAX_DELAY);
    this->delete_task_resources();
}

void Imu::delete_task_resources() {
    if (this->bno_mutex) {
        vSemaphoreDelete(this->bno_mutex);
    }
    if (this->sample_queue) {
        vQueueDelete(this->sample_queue);
    }
    if (this->read_task_stopped) {
        vSemaphoreDelete(this->read_task_stopped);
    }
}

void Imu::read_loop(void *imu) {
    Imu *const self = static_cast<Imu *>(imu);
    TickType_t last_wake = xTaskGetTickCount();
    while (!self->stop_requested.load()) {
        bool failed = false;
        try {
            const Sample sample = self->read_sample(self->requested_data_select.load());
            xQueueOverwrite(self->sample_queue, &sample);
        } catch (const std::exception &) {
            failed = true;
        }
        self->read_failed.store(failed);
        if (failed) {
            // A dead sensor fails every read after all retry rounds; back off instead of hammering the bus.
            self->failed_reads.fetch_add(1);
            ulTaskNotifyTake(pdTRUE, RETRY_DELAY);
            last_wake = xTaskGetTickCount();
        } else if (xTaskDelayUntil(&last_wake, READ_PERIOD) == pdFALSE) {
            // Reading all nine blocks takes longer than the period, so this usually overruns:
            // re-anchor and yield one tick so the idle task keeps feeding the watchdog.
            last_wake = xTaskGetTickCount();
            vTaskDelay(1);
        }
    }
    // The destructor frees `self` once it is given the semaphore, so copy the handle first.
    const SemaphoreHandle_t stopped = self->read_task_stopped;
    xSemaphoreGive(stopped);
    vTaskDelete(nullptr);
}

Imu::Sample Imu::read_sample(const uint16_t data_select) const {
    Sample sample{};
    sample.data_select = data_select;
    const MutexGuard guard(this->bno_mutex);
    if (data_select & 0x0001) {
        sample.calibration = this->bno->getCalibration();
    }
    if (data_select & 0x0002) {
        sample.accelerometer = this->bno->getVectorAccelerometer();
    }
    if (data_select & 0x0004) {
        sample.magnetometer = this->bno->getVectorMagnetometer();
    }
    if (data_select & 0x0008) {
        sample.gyroscope = this->bno->getVectorGyroscope();
    }
    if (data_select & 0x0010) {
        sample.euler = this->bno->getVectorEuler();
    }
    if (data_select & 0x0020) {
        sample.quaternion = this->bno->getQuaternion();
    }
    if (data_select & 0x0040) {
        sample.linear_acceleration = this->bno->getVectorLinearAccel();
    }
    if (data_select & 0x0080) {
        sample.gravity = this->bno->getVectorGravity();
    }
    if (data_select & 0x0100) {
        sample.temperature = this->bno->getTemp();
    }
    return sample;
}

void Imu::step() {
    this->requested_data_select.store(this->properties.at("data_select")->integer_value());

    Sample sample;
    if (xQueueReceive(this->sample_queue, &sample, 0) == pdTRUE) {
        this->publish(sample);
    }

    Module::step();

    // Every failed read is reported, which the retry delay limits to at most once per second while the sensor is down.
    const uint32_t failed_reads = this->failed_reads.load();
    if (failed_reads != this->reported_failed_reads) {
        this->reported_failed_reads = failed_reads;
        this->read_failure_reported = true;
        throw std::runtime_error("reading the imu failed, properties keep their last values");
    }
    if (this->read_failure_reported && !this->read_failed.load()) {
        this->read_failure_reported = false;
        echo("module \"%s\": reading the imu works again", this->name.c_str());
    }
}

void Imu::publish(const Sample &sample) {
    if (sample.data_select & 0x0001) {
        this->properties.at("cal_sys")->set_integer_value(sample.calibration.sys);
        this->properties.at("cal_gyr")->set_integer_value(sample.calibration.gyro);
        this->properties.at("cal_acc")->set_integer_value(sample.calibration.accel);
        this->properties.at("cal_mag")->set_integer_value(sample.calibration.mag);
    }
    if (sample.data_select & 0x0002) {
        this->properties.at("acc_x")->set_number_value(sample.accelerometer.x);
        this->properties.at("acc_y")->set_number_value(sample.accelerometer.y);
        this->properties.at("acc_z")->set_number_value(sample.accelerometer.z);
    }
    if (sample.data_select & 0x0004) {
        this->properties.at("mag_x")->set_number_value(sample.magnetometer.x);
        this->properties.at("mag_y")->set_number_value(sample.magnetometer.y);
        this->properties.at("mag_z")->set_number_value(sample.magnetometer.z);
    }
    if (sample.data_select & 0x0008) {
        this->properties.at("gyr_x")->set_number_value(sample.gyroscope.x);
        this->properties.at("gyr_y")->set_number_value(sample.gyroscope.y);
        this->properties.at("gyr_z")->set_number_value(sample.gyroscope.z);
    }
    if (sample.data_select & 0x0010) {
        this->properties.at("yaw")->set_number_value(sample.euler.x);
        this->properties.at("roll")->set_number_value(sample.euler.y);
        this->properties.at("pitch")->set_number_value(sample.euler.z);
    }
    if (sample.data_select & 0x0020) {
        this->properties.at("quat_w")->set_number_value(sample.quaternion.w);
        this->properties.at("quat_x")->set_number_value(sample.quaternion.x);
        this->properties.at("quat_y")->set_number_value(sample.quaternion.y);
        this->properties.at("quat_z")->set_number_value(sample.quaternion.z);
    }
    if (sample.data_select & 0x0040) {
        this->properties.at("lin_x")->set_number_value(sample.linear_acceleration.x);
        this->properties.at("lin_y")->set_number_value(sample.linear_acceleration.y);
        this->properties.at("lin_z")->set_number_value(sample.linear_acceleration.z);
    }
    if (sample.data_select & 0x0080) {
        this->properties.at("grav_x")->set_number_value(sample.gravity.x);
        this->properties.at("grav_y")->set_number_value(sample.gravity.y);
        this->properties.at("grav_z")->set_number_value(sample.gravity.z);
    }
    if (sample.data_select & 0x0100) {
        this->properties.at("temp")->set_integer_value(sample.temperature);
    }
}

void Imu::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "set_mode") {
        Module::expect(arguments, 1, string);
        std::string mode = arguments[0]->evaluate_string();
        std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
        try {
            const MutexGuard guard(this->bno_mutex);
            if (mode == "configmode") {
                this->bno->setOprModeConfig();
            } else if (mode == "acconly") {
                this->bno->setOprModeAccOnly();
            } else if (mode == "magonly") {
                this->bno->setOprModeMagOnly();
            } else if (mode == "gyroonly") {
                this->bno->setOprModeGyroOnly();
            } else if (mode == "accmag") {
                this->bno->setOprModeAccMag();
            } else if (mode == "accgyro") {
                this->bno->setOprModeAccGyro();
            } else if (mode == "maggyro") {
                this->bno->setOprModeMagGyro();
            } else if (mode == "amg") {
                this->bno->setOprModeAMG();
            } else if (mode == "imu") {
                this->bno->setOprModeIMU();
            } else if (mode == "compass") {
                this->bno->setOprModeCompass();
            } else if (mode == "m4g") {
                this->bno->setOprModeM4G();
            } else if (mode == "ndof_fmc_off") {
                this->bno->setOprModeNdofFmcOff();
            } else if (mode == "ndof") {
                this->bno->setOprModeNdof();
            } else {
                throw std::runtime_error("invalid mode: " + mode);
            }
        } catch (std::exception &ex) {
            throw std::runtime_error(std::string("setting imu mode failed: ") + ex.what());
        }
    } else {
        Module::call(method_name, arguments);
    }
}
