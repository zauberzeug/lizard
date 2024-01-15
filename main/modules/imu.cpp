#include "imu.h"

#define I2C_MASTER_TX_BUF_DISABLE 0
#define I2C_MASTER_RX_BUF_DISABLE 0

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
        this->bno->setOprModeM4G(); //setOprModeM4G
    } catch (BNO055BaseException &ex) {
        throw std::runtime_error(std::string("imu setup failed: ") + ex.what());
    } catch (std::exception &ex) {
        throw std::runtime_error(std::string("imu setup failed: ") + ex.what());
    }
    this->properties["roll"] = std::make_shared<NumberVariable>();
    this->properties["tilt"] = std::make_shared<NumberVariable>();
    this->properties["yaw"] = std::make_shared<NumberVariable>();
    this->properties["acc_x"] = std::make_shared<NumberVariable>();
    this->properties["acc_y"] = std::make_shared<NumberVariable>();
    this->properties["acc_z"] = std::make_shared<NumberVariable>();
}

void Imu::step() {
    bno055_vector_t v = this->bno->getVectorAccelerometer();
    this->properties.at("acc_x")->number_value = v.x;
    this->properties.at("acc_y")->number_value = v.y;
    this->properties.at("acc_z")->number_value = v.z;

    bno055_vector_t e = this->bno->getVectorEuler();
    this->properties.("roll")->number_value = e.x;
    this->properties.("tilt")->number_value = e.y;
    this->properties.("yaw")->number_value = e.z;
    Module::step();
}