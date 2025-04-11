#include "imu.h"
#include <stdexcept>

#define I2C_MASTER_TX_BUF_DISABLE 0
#define I2C_MASTER_RX_BUF_DISABLE 0

REGISTER_MODULE_DEFAULTS(Imu)

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
    : Module(imu, name), i2c_port(i2c_port), address(address) {
    i2c_config_t config;
    config.mode = I2C_MODE_MASTER;
    config.sda_io_num = sda_pin,
    config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    config.scl_io_num = scl_pin;
    config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    config.master.clk_speed = clk_speed;
    config.clk_flags = 0;
    if (i2c_param_config(i2c_port, &config) != ESP_OK) {
        throw std::runtime_error("could not configure i2c port");
    }
    if (i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_TX_BUF_DISABLE, I2C_MASTER_RX_BUF_DISABLE, 0) != ESP_OK) {
        throw std::runtime_error("could not install i2c driver");
    }
    if (i2c_set_timeout(i2c_port, 1048575) != ESP_OK) {
        throw std::runtime_error("could not set i2c timeout");
    }
    this->bno = std::make_shared<BNO055>((i2c_port_t)i2c_port, address);
    try {
        this->bno->begin();
        this->bno->enableExternalCrystal();
        this->bno->setOprModeNdof();
    } catch (std::exception &ex) {
        throw std::runtime_error(std::string("imu setup failed: ") + ex.what());
    }
    this->properties = Imu::get_defaults();
}

void Imu::step() {
    uint16_t data_select = this->properties.at("data_select")->integer_value;

    if (data_select & 0x0001) {
        bno055_calibration_t c = this->bno->getCalibration();
        this->properties.at("cal_sys")->integer_value = c.sys;
        this->properties.at("cal_gyr")->integer_value = c.gyro;
        this->properties.at("cal_acc")->integer_value = c.accel;
        this->properties.at("cal_mag")->integer_value = c.mag;
    }
    if (data_select & 0x0002) {
        bno055_vector_t v = this->bno->getVectorAccelerometer();
        this->properties.at("acc_x")->number_value = v.x;
        this->properties.at("acc_y")->number_value = v.y;
        this->properties.at("acc_z")->number_value = v.z;
    }
    if (data_select & 0x0004) {
        bno055_vector_t v = this->bno->getVectorMagnetometer();
        this->properties.at("mag_x")->number_value = v.x;
        this->properties.at("mag_y")->number_value = v.y;
        this->properties.at("mag_z")->number_value = v.z;
    }
    if (data_select & 0x0008) {
        bno055_vector_t v = this->bno->getVectorGyroscope();
        this->properties.at("gyr_x")->number_value = v.x;
        this->properties.at("gyr_y")->number_value = v.y;
        this->properties.at("gyr_z")->number_value = v.z;
    }
    if (data_select & 0x0010) {
        bno055_vector_t v = this->bno->getVectorEuler();
        this->properties.at("yaw")->number_value = v.x;
        this->properties.at("roll")->number_value = v.y;
        this->properties.at("pitch")->number_value = v.z;
    }
    if (data_select & 0x0020) {
        bno055_quaternion_t q = this->bno->getQuaternion();
        this->properties.at("quat_w")->number_value = q.w;
        this->properties.at("quat_x")->number_value = q.x;
        this->properties.at("quat_y")->number_value = q.y;
        this->properties.at("quat_z")->number_value = q.z;
    }
    if (data_select & 0x0040) {
        bno055_vector_t v = this->bno->getVectorLinearAccel();
        this->properties.at("lin_x")->number_value = v.x;
        this->properties.at("lin_y")->number_value = v.y;
        this->properties.at("lin_z")->number_value = v.z;
    }
    if (data_select & 0x0080) {
        bno055_vector_t g = this->bno->getVectorGravity();
        this->properties.at("grav_x")->number_value = g.x;
        this->properties.at("grav_y")->number_value = g.y;
        this->properties.at("grav_z")->number_value = g.z;
    }
    if (data_select & 0x0100) {
        this->properties.at("temp")->number_value = this->bno->getTemp();
    }

    Module::step();
}

void Imu::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "set_mode") {
        Module::expect(arguments, 1, string);
        std::string mode = arguments[0]->evaluate_string();
        std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
        try {
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
