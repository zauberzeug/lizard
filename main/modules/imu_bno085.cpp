#include "imu_bno085.h"
#include "i2c_bus.h"
#include <algorithm>
#include <stdexcept>

#include "esp_log.h"

REGISTER_MODULE_DEFAULTS(ImuBno085)

namespace {
constexpr char TAG[] = "ImuBno085";
constexpr int MAX_EVENTS_PER_STEP = 16;
constexpr uint32_t REPORT_INTERVAL_US = 100000UL;

inline int accuracy_from_status(uint8_t status) {
    return static_cast<int>(status & 0x03U);
}
} // namespace

const std::map<std::string, Variable_ptr> ImuBno085::get_defaults() {
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

void ImuBno085::enable_default_reports() {
    apply_mode(current_mode);
}

void ImuBno085::apply_mode(const std::string &mode) {
    static constexpr sh2_SensorId_t all_sensors[] = {
        SH2_ACCELEROMETER,
        SH2_MAGNETIC_FIELD_CALIBRATED,
        SH2_GYROSCOPE_CALIBRATED,
        SH2_GAME_ROTATION_VECTOR,
        SH2_LINEAR_ACCELERATION,
        SH2_GRAVITY,
        SH2_TEMPERATURE,
    };
    static constexpr size_t N = std::size(all_sensors);

    // Build desired state: which sensors should be enabled for this mode
    // Indices: 0=acc, 1=mag, 2=gyr, 3=rot, 4=lin, 5=grav, 6=temp
    std::array<bool, N> desired{};

    if (mode == "configmode") {
        // all false
    } else if (mode == "acconly") {
        desired[0] = true;
    } else if (mode == "magonly") {
        desired[1] = true;
    } else if (mode == "gyroonly") {
        desired[2] = true;
    } else if (mode == "accmag") {
        desired[0] = true;
        desired[1] = true;
    } else if (mode == "accgyro") {
        desired[0] = true;
        desired[2] = true;
    } else if (mode == "maggyro") {
        desired[1] = true;
        desired[2] = true;
    } else if (mode == "amg") {
        desired[0] = true;
        desired[1] = true;
        desired[2] = true;
    } else if (mode == "imu") {
        desired[0] = true;
        desired[2] = true;
        desired[3] = true;
    } else if (mode == "compass") {
        desired[1] = true;
        desired[3] = true;
    } else if (mode == "m4g") {
        desired[0] = true;
        desired[1] = true;
        desired[3] = true;
    } else if (mode == "ndof_fmc_off") {
        desired[0] = true;
        desired[1] = true;
        desired[2] = true;
        desired[3] = true;
    } else if (mode == "ndof") {
        desired.fill(true);
    } else {
        throw std::runtime_error("invalid mode: " + mode);
    }

    // Only send commands for sensors whose state actually changes
    for (size_t i = 0; i < N; ++i) {
        if (desired[i] != active_reports[i]) {
            if (!bno->enableReport(all_sensors[i], desired[i] ? REPORT_INTERVAL_US : 0)) {
                throw std::runtime_error("unable to configure sensor report");
            }
            active_reports[i] = desired[i];
        }
    }
}

ImuBno085::ImuBno085(const std::string name, i2c_port_t i2c_port, gpio_num_t sda_pin, gpio_num_t scl_pin,
                     gpio_num_t int_pin, gpio_num_t rst_pin, uint8_t address, int clk_speed)
    : Module(imu_bno085, name), i2c_port(i2c_port), sda_pin(sda_pin), scl_pin(scl_pin),
      int_pin(int_pin), rst_pin(rst_pin), address(address), clk_speed(clk_speed) {
    I2cBusManager::ensure(i2c_port, sda_pin, scl_pin, clk_speed);
    bno = std::make_unique<Bno08x>(rst_pin);

    if (!bno->begin_I2C(i2c_port, address, int_pin)) {
        throw std::runtime_error("BNO085 initialization failed");
    }

    enable_default_reports();

    this->properties = ImuBno085::get_defaults();
}

void ImuBno085::step() {
    if (bno->wasReset()) {
        ESP_LOGW(TAG, "BNO085 reset detected, re-enabling reports");
        enable_default_reports();
    }

    const uint16_t data_select = this->properties.at("data_select")->integer_value;
    sh2_SensorValue_t sensor_value{};

    for (int i = 0; i < MAX_EVENTS_PER_STEP && bno->getSensorEvent(&sensor_value); ++i) {
        switch (sensor_value.sensorId) {
        case SH2_ACCELEROMETER:
            if (data_select & 0x0002) {
                this->properties.at("acc_x")->number_value = sensor_value.un.accelerometer.x;
                this->properties.at("acc_y")->number_value = sensor_value.un.accelerometer.y;
                this->properties.at("acc_z")->number_value = sensor_value.un.accelerometer.z;
            }
            if (data_select & 0x0001) {
                this->properties.at("cal_acc")->integer_value = accuracy_from_status(sensor_value.status);
            }
            break;
        case SH2_GYROSCOPE_CALIBRATED:
            if (data_select & 0x0008) {
                this->properties.at("gyr_x")->number_value = sensor_value.un.gyroscope.x;
                this->properties.at("gyr_y")->number_value = sensor_value.un.gyroscope.y;
                this->properties.at("gyr_z")->number_value = sensor_value.un.gyroscope.z;
            }
            if (data_select & 0x0001) {
                this->properties.at("cal_gyr")->integer_value = accuracy_from_status(sensor_value.status);
            }
            break;
        case SH2_MAGNETIC_FIELD_CALIBRATED:
            if (data_select & 0x0004) {
                this->properties.at("mag_x")->number_value = sensor_value.un.magneticField.x;
                this->properties.at("mag_y")->number_value = sensor_value.un.magneticField.y;
                this->properties.at("mag_z")->number_value = sensor_value.un.magneticField.z;
            }
            if (data_select & 0x0001) {
                this->properties.at("cal_mag")->integer_value = accuracy_from_status(sensor_value.status);
            }
            break;
        case SH2_GAME_ROTATION_VECTOR:
            if (data_select & 0x0010) {
                this->properties.at("quat_w")->number_value = sensor_value.un.gameRotationVector.real;
                this->properties.at("quat_x")->number_value = sensor_value.un.gameRotationVector.i;
                this->properties.at("quat_y")->number_value = sensor_value.un.gameRotationVector.j;
                this->properties.at("quat_z")->number_value = sensor_value.un.gameRotationVector.k;
            }
            if (data_select & 0x0001) {
                this->properties.at("cal_sys")->integer_value = accuracy_from_status(sensor_value.status);
            }
            break;
        case SH2_LINEAR_ACCELERATION:
            if (data_select & 0x0020) {
                this->properties.at("lin_x")->number_value = sensor_value.un.linearAcceleration.x;
                this->properties.at("lin_y")->number_value = sensor_value.un.linearAcceleration.y;
                this->properties.at("lin_z")->number_value = sensor_value.un.linearAcceleration.z;
            }
            break;
        case SH2_GRAVITY:
            if (data_select & 0x0040) {
                this->properties.at("grav_x")->number_value = sensor_value.un.gravity.x;
                this->properties.at("grav_y")->number_value = sensor_value.un.gravity.y;
                this->properties.at("grav_z")->number_value = sensor_value.un.gravity.z;
            }
            break;
        case SH2_TEMPERATURE:
            if (data_select & 0x0080) {
                this->properties.at("temp")->integer_value = static_cast<int>(sensor_value.un.temperature.value);
            }
            break;
        default:
            break;
        }
    }

    Module::step();
}

void ImuBno085::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "set_mode") {
        Module::expect(arguments, 1, string);
        std::string mode = arguments[0]->evaluate_string();
        std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
        // BNO055 compatibility modes — the BNO085 uses report-based configuration rather than hardware modes.
        // These presets emulate BNO055 modes by enabling/disabling the corresponding sensor reports.
        try {
            apply_mode(mode);
            current_mode = mode;
        } catch (std::exception &ex) {
            throw std::runtime_error(std::string("setting imu mode failed: ") + ex.what());
        }
    } else {
        Module::call(method_name, arguments);
    }
}
