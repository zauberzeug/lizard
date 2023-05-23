#include "rmd_motor.h"
#include "../global.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include <cstring>
#include <memory>

RmdMotor::RmdMotor(const std::string name, const Can_ptr can, const uint8_t motor_id)
    : Module(rmd_motor, name), motor_id(motor_id), can(can), is_version_3(true) {
    this->properties["position"] = std::make_shared<NumberVariable>();
    this->properties["ratio"] = std::make_shared<NumberVariable>(6.0);
    this->properties["torque"] = std::make_shared<NumberVariable>();
    this->properties["speed"] = std::make_shared<NumberVariable>();
    this->properties["temperature"] = std::make_shared<NumberVariable>();
    this->properties["can_age"] = std::make_shared<NumberVariable>();
}

void RmdMotor::subscribe_to_can() {
    can->subscribe(this->motor_id + 0x140, std::static_pointer_cast<Module>(this->shared_from_this()));
    can->subscribe(this->motor_id + 0x240, std::static_pointer_cast<Module>(this->shared_from_this()));
}

void RmdMotor::send(const uint8_t d0, const uint8_t d1, const uint8_t d2, const uint8_t d3,
                    const uint8_t d4, const uint8_t d5, const uint8_t d6, const uint8_t d7) {
    this->can->send(this->motor_id + 0x140, d0, d1, d2, d3, d4, d5, d6, d7);
}

void RmdMotor::step() {
    this->properties.at("can_age")->number_value = millis_since(this->last_msg_millis) / 1e3;

    this->send(0x9c, 0, 0, 0, 0, 0, 0, 0);
    Module::step();
}

void RmdMotor::power(double target_power) {
    int16_t power = this->is_version_3 ? target_power * 100 : target_power / 32.0 * 2000;
    power *= this->properties.at("ratio")->number_value < 0 ? -1 : 1;
    this->send(0xa1, 0,
               0,
               0,
               *((uint8_t *)(&power) + 0),
               *((uint8_t *)(&power) + 1),
               0,
               0);
}

void RmdMotor::speed(double target_speed) {
    int32_t speed = target_speed * 100 * this->properties.at("ratio")->number_value;
    this->send(0xa2, 0,
               0,
               0,
               *((uint8_t *)(&speed) + 0),
               *((uint8_t *)(&speed) + 1),
               *((uint8_t *)(&speed) + 2),
               *((uint8_t *)(&speed) + 3));
}

void RmdMotor::position(double target_position, double target_speed) {
    int32_t position = target_position * 100 * this->properties.at("ratio")->number_value;
    uint16_t speed = target_speed * abs(this->properties.at("ratio")->number_value);
    this->send(0xa4, 0,
               *((uint8_t *)(&speed) + 0),
               *((uint8_t *)(&speed) + 1),
               *((uint8_t *)(&position) + 0),
               *((uint8_t *)(&position) + 1),
               *((uint8_t *)(&position) + 2),
               *((uint8_t *)(&position) + 3));
}

void RmdMotor::stop() {
    this->send(0x81, 0, 0, 0, 0, 0, 0, 0);
}

void RmdMotor::off() {
    this->send(0x80, 0, 0, 0, 0, 0, 0, 0);
}

void RmdMotor::hold() {
    this->position(this->properties.at("position")->number_value);
}

void RmdMotor::clear_errors() {
    this->send(this->is_version_3 ? 0x76 : 0x9b, 0, 0, 0, 0, 0, 0, 0);
}

void RmdMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "zero") {
        Module::expect(arguments, 0);
        if (this->is_version_3) {
            this->send(0x64, 0, 0, 0, 0, 0, 0, 0);
            this->send(0x76, 0, 0, 0, 0, 0, 0, 0);
        } else {
            this->send(0x19, 0, 0, 0, 0, 0, 0, 0);
        }
    } else if (method_name == "power") {
        Module::expect(arguments, 1, numbery);
        this->power(arguments[0]->evaluate_number());
    } else if (method_name == "speed") {
        Module::expect(arguments, 1, numbery);
        this->speed(arguments[0]->evaluate_number());
    } else if (method_name == "position") {
        if (arguments.size() == 1) {
            Module::expect(arguments, 1, numbery);
            this->position(arguments[0]->evaluate_number(), 0);
        } else {
            Module::expect(arguments, 2, numbery, numbery);
            this->position(arguments[0]->evaluate_number(), arguments[1]->evaluate_number());
        }
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->stop();
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->off();
    } else if (method_name == "hold") {
        Module::expect(arguments, 0);
        this->hold();
    } else if (method_name == "get_health") {
        Module::expect(arguments, 0);
        this->send(0x9a, 0, 0, 0, 0, 0, 0, 0);
    } else if (method_name == "get_pid") {
        Module::expect(arguments, 0);
        this->send(0x30, 0, 0, 0, 0, 0, 0, 0);
    } else if (method_name == "set_pid") {
        Module::expect(arguments, 6, integer, integer, integer, integer, integer, integer);
        this->send(0x32, 0,
                   arguments[this->is_version_3 ? 4 : 0]->evaluate_integer(),
                   arguments[this->is_version_3 ? 5 : 1]->evaluate_integer(),
                   arguments[this->is_version_3 ? 2 : 2]->evaluate_integer(),
                   arguments[this->is_version_3 ? 3 : 3]->evaluate_integer(),
                   arguments[this->is_version_3 ? 0 : 4]->evaluate_integer(),
                   arguments[this->is_version_3 ? 1 : 5]->evaluate_integer());
    } else if (method_name == "clear_errors") {
        Module::expect(arguments, 0);
        this->clear_errors();
    } else {
        Module::call(method_name, arguments);
    }
}

void RmdMotor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    this->is_version_3 = id > 0x240;
    switch (data[0]) {
    case 0x9c: {
        int8_t temperature = 0;
        std::memcpy(&temperature, data + 1, 1);
        this->properties.at("temperature")->number_value = temperature;

        int16_t torque = 0;
        std::memcpy(&torque, data + 2, 2);
        this->properties.at("torque")->number_value = this->is_version_3 ? 0.01 * torque : torque / 2048.0 * 33.0;

        int16_t speed = 0;
        std::memcpy(&speed, data + 4, 2);
        this->properties.at("speed")->number_value = speed / this->properties.at("ratio")->number_value;

        int16_t position = 0;
        std::memcpy(&position, data + 6, 2);
        this->properties.at("position")->number_value = position / this->properties.at("ratio")->number_value;

        break;
    }
    case 0x30: {
        echo("%s pid %3d %3d %3d %3d %3d %3d",
             this->name.c_str(),
             data[this->is_version_3 ? 6 : 2],
             data[this->is_version_3 ? 7 : 3],
             data[this->is_version_3 ? 4 : 4],
             data[this->is_version_3 ? 5 : 5],
             data[this->is_version_3 ? 2 : 6],
             data[this->is_version_3 ? 3 : 7]);
        break;
    }
    }
    this->last_msg_millis = millis();
}

double RmdMotor::get_position() const {
    return this->properties.at("position")->number_value;
}

double RmdMotor::get_speed() const {
    return this->properties.at("speed")->number_value;
}
