#include "imu_bno085.h"
#include "i2c_bus.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

REGISTER_MODULE_DEFAULTS(ImuBno085)

namespace {
constexpr double RAD_TO_DEG = 180.0 / M_PI;

inline int accuracy_from_status(uint8_t status) {
    return static_cast<int>(status & 0x03U);
}

template <typename Quaternion>
inline void quaternion_to_euler(const Quaternion &quat, double &roll, double &pitch, double &yaw) {
    double w = quat.real;
    double x = quat.i;
    double y = quat.j;
    double z = quat.k;

    double t0 = +2.0 * (w * x + y * z);
    double t1 = +1.0 - 2.0 * (x * x + y * y);
    roll = std::atan2(t0, t1);

    double t2 = +2.0 * (w * y - z * x);
    t2 = std::clamp(t2, -1.0, 1.0);
    pitch = std::asin(t2);

    double t3 = +2.0 * (w * z + x * y);
    double t4 = +1.0 - 2.0 * (y * y + z * z);
    yaw = std::atan2(t3, t4);
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

ImuBno085::ImuBno085(const std::string name, i2c_port_t i2c_port, gpio_num_t sda_pin, gpio_num_t scl_pin, gpio_num_t int_pin,
                     gpio_num_t rst_pin, uint8_t address, int clk_speed)
    : Module(imu, name), i2c_port(i2c_port), sda_pin(sda_pin), scl_pin(scl_pin), int_pin(int_pin), rst_pin(rst_pin),
      address(address), clk_speed(clk_speed), report_interval_us(100000UL) {
    I2cBusManager::ensure(i2c_port, sda_pin, scl_pin, clk_speed);
    bno = std::make_unique<Adafruit_BNO08x>(rst_pin);

    if (!bno->begin_I2C(i2c_port, address, int_pin)) {
        throw std::runtime_error("BNO085 initialization failed");
    }

    auto enable_report = [&](sh2_SensorId_t sensor) {
        if (!bno->enableReport(sensor, report_interval_us)) {
            throw std::runtime_error("unable to enable sensor report");
        }
    };

    enable_report(SH2_ACCELEROMETER);
    enable_report(SH2_GYROSCOPE_CALIBRATED);
    enable_report(SH2_MAGNETIC_FIELD_CALIBRATED);
    enable_report(SH2_GAME_ROTATION_VECTOR);
    enable_report(SH2_LINEAR_ACCELERATION);
    enable_report(SH2_GRAVITY);
    enable_report(SH2_ROTATION_VECTOR);
    enable_report(SH2_TEMPERATURE);

    this->properties = ImuBno085::get_defaults();
}

void ImuBno085::step() {
    uint16_t data_select = this->properties.at("data_select")->integer_value;
    sh2_SensorValue_t sensor_value{};

    while (bno->getSensorEvent(&sensor_value)) {
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
            if (data_select & 0x0020) {
                this->properties.at("quat_w")->number_value = sensor_value.un.gameRotationVector.real;
                this->properties.at("quat_x")->number_value = sensor_value.un.gameRotationVector.i;
                this->properties.at("quat_y")->number_value = sensor_value.un.gameRotationVector.j;
                this->properties.at("quat_z")->number_value = sensor_value.un.gameRotationVector.k;
            }
            if (data_select & 0x0001) {
                this->properties.at("cal_sys")->integer_value = accuracy_from_status(sensor_value.status);
            }
            break;
        case SH2_ROTATION_VECTOR:
            if (data_select & 0x0010) {
                double roll, pitch, yaw;
                quaternion_to_euler(sensor_value.un.rotationVector, roll, pitch, yaw);
                this->properties.at("roll")->number_value = roll * RAD_TO_DEG;
                this->properties.at("pitch")->number_value = pitch * RAD_TO_DEG;
                this->properties.at("yaw")->number_value = yaw * RAD_TO_DEG;
            }
            break;
        case SH2_LINEAR_ACCELERATION:
            if (data_select & 0x0040) {
                this->properties.at("lin_x")->number_value = sensor_value.un.linearAcceleration.x;
                this->properties.at("lin_y")->number_value = sensor_value.un.linearAcceleration.y;
                this->properties.at("lin_z")->number_value = sensor_value.un.linearAcceleration.z;
            }
            break;
        case SH2_GRAVITY:
            if (data_select & 0x0080) {
                this->properties.at("grav_x")->number_value = sensor_value.un.gravity.x;
                this->properties.at("grav_y")->number_value = sensor_value.un.gravity.y;
                this->properties.at("grav_z")->number_value = sensor_value.un.gravity.z;
            }
            break;
        case SH2_TEMPERATURE:
            if (data_select & 0x0100) {
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

        auto set_report = [&](sh2_SensorId_t sensor, bool enable) {
            if (!bno->enableReport(sensor, enable ? report_interval_us : 0)) {
                throw std::runtime_error("unable to configure sensor report");
            }
        };

        auto disable_all = [&]() {
            set_report(SH2_ACCELEROMETER, false);
            set_report(SH2_MAGNETIC_FIELD_CALIBRATED, false);
            set_report(SH2_GYROSCOPE_CALIBRATED, false);
            set_report(SH2_GAME_ROTATION_VECTOR, false);
            set_report(SH2_ROTATION_VECTOR, false);
            set_report(SH2_LINEAR_ACCELERATION, false);
            set_report(SH2_GRAVITY, false);
            set_report(SH2_TEMPERATURE, false);
        };

        try { // TODO: check if the BNO085 has different modes than the BNO055
            disable_all();
            if (mode == "configmode") {
                // no reports
            } else if (mode == "acconly") {
                set_report(SH2_ACCELEROMETER, true);
            } else if (mode == "magonly") {
                set_report(SH2_MAGNETIC_FIELD_CALIBRATED, true);
            } else if (mode == "gyroonly") {
                set_report(SH2_GYROSCOPE_CALIBRATED, true);
            } else if (mode == "accmag") {
                set_report(SH2_ACCELEROMETER, true);
                set_report(SH2_MAGNETIC_FIELD_CALIBRATED, true);
            } else if (mode == "accgyro") {
                set_report(SH2_ACCELEROMETER, true);
                set_report(SH2_GYROSCOPE_CALIBRATED, true);
            } else if (mode == "maggyro") {
                set_report(SH2_MAGNETIC_FIELD_CALIBRATED, true);
                set_report(SH2_GYROSCOPE_CALIBRATED, true);
            } else if (mode == "amg") {
                set_report(SH2_ACCELEROMETER, true);
                set_report(SH2_MAGNETIC_FIELD_CALIBRATED, true);
                set_report(SH2_GYROSCOPE_CALIBRATED, true);
            } else if (mode == "imu") {
                set_report(SH2_ACCELEROMETER, true);
                set_report(SH2_GYROSCOPE_CALIBRATED, true);
                set_report(SH2_GAME_ROTATION_VECTOR, true);
                set_report(SH2_ROTATION_VECTOR, true);
            } else if (mode == "compass") {
                set_report(SH2_MAGNETIC_FIELD_CALIBRATED, true);
                set_report(SH2_ROTATION_VECTOR, true);
            } else if (mode == "m4g") {
                set_report(SH2_ACCELEROMETER, true);
                set_report(SH2_MAGNETIC_FIELD_CALIBRATED, true);
                set_report(SH2_ROTATION_VECTOR, true);
            } else if (mode == "ndof_fmc_off") {
                set_report(SH2_ACCELEROMETER, true);
                set_report(SH2_MAGNETIC_FIELD_CALIBRATED, true);
                set_report(SH2_GYROSCOPE_CALIBRATED, true);
                set_report(SH2_GAME_ROTATION_VECTOR, true);
                set_report(SH2_ROTATION_VECTOR, true);
            } else if (mode == "ndof") {
                set_report(SH2_ACCELEROMETER, true);
                set_report(SH2_MAGNETIC_FIELD_CALIBRATED, true);
                set_report(SH2_GYROSCOPE_CALIBRATED, true);
                set_report(SH2_GAME_ROTATION_VECTOR, true);
                set_report(SH2_ROTATION_VECTOR, true);
                set_report(SH2_LINEAR_ACCELERATION, true);
                set_report(SH2_GRAVITY, true);
                set_report(SH2_TEMPERATURE, true);
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