#include "rmd_motor.h"
#include "../global.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include <cstring>
#include <inttypes.h>
#include <math.h>
#include <memory>

RmdMotor::RmdMotor(const std::string name, const Can_ptr can, const uint8_t motor_id, const int ratio)
    : Module(rmd_motor, name), motor_id(motor_id), can(can), ratio(ratio) {
    this->properties["position"] = std::make_shared<NumberVariable>();
    this->properties["can_age"] = std::make_shared<NumberVariable>();
}

void RmdMotor::subscribe_to_can() {
    can->subscribe(this->motor_id + 0x240, std::static_pointer_cast<Module>(this->shared_from_this()));
}

void RmdMotor::send(const uint8_t d0, const uint8_t d1, const uint8_t d2, const uint8_t d3,
                    const uint8_t d4, const uint8_t d5, const uint8_t d6, const uint8_t d7,
                    const unsigned long int timeout_ms) {
    this->last_msg_id = 0;
    const int max_attempts = 3;
    for (int i = 0; i < max_attempts; ++i) {
        this->can->send(this->motor_id + 0x140, d0, d1, d2, d3, d4, d5, d6, d7);
        unsigned long int start = micros();
        while (this->last_msg_id != d0 && micros_since(start) < timeout_ms * 1000) {
            this->can->receive();
        }
        if (this->last_msg_id == d0) {
            return;
        } else {
            echo("%s warning: CAN timeout for msg id 0x%02x (attempt %d/%d)", this->name.c_str(), d0, i + 1, max_attempts);
        }
    }
}

void RmdMotor::step() {
    this->properties.at("can_age")->number_value = millis_since(this->last_msg_millis) / 1e3;

    this->send(0x60, 0, 0, 0, 0, 0, 0, 0);
    Module::step();
}

void RmdMotor::power(double target_power) {
    int16_t power = target_power * 100;
    this->send(0xa1, 0,
               0,
               0,
               *((uint8_t *)(&power) + 0),
               *((uint8_t *)(&power) + 1),
               0,
               0);
}

void RmdMotor::speed(double target_speed) {
    int32_t speed = target_speed * 100;
    this->send(0xa2, 0,
               0,
               0,
               *((uint8_t *)(&speed) + 0),
               *((uint8_t *)(&speed) + 1),
               *((uint8_t *)(&speed) + 2),
               *((uint8_t *)(&speed) + 3));
}

void RmdMotor::position(double target_position, double target_speed) {
    int32_t position = target_position * 100;
    uint16_t speed = target_speed;
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
    this->send(0x76, 0, 0, 0, 0, 0, 0, 0);
}

void RmdMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "power") {
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
    } else if (method_name == "get_pid") {
        Module::expect(arguments, 0);
        this->send(0x30, 0, 0, 0, 0, 0, 0, 0);
    } else if (method_name == "set_pid") {
        Module::expect(arguments, 6, integer, integer, integer, integer, integer, integer);
        this->send(0x32, 0,
                   arguments[4]->evaluate_integer(),
                   arguments[5]->evaluate_integer(),
                   arguments[2]->evaluate_integer(),
                   arguments[3]->evaluate_integer(),
                   arguments[0]->evaluate_integer(),
                   arguments[1]->evaluate_integer());
    } else if (method_name == "get_acceleration") {
        Module::expect(arguments, 0);
        this->send(0x42, 0, 0, 0, 0, 0, 0, 0);
    } else if (method_name == "set_acceleration") {
        Module::expect(arguments, 4, integer, integer, integer, integer);
        for (uint8_t i = 0; i < 4; ++i) {
            int acceleration = arguments[i]->evaluate_integer();
            if (acceleration > 0) {
                set_acceleration(i, acceleration);
            }
        }
    } else if (method_name == "get_status") {
        Module::expect(arguments, 0);
        this->send(0x9a, 0, 0, 0, 0, 0, 0, 0);
    } else if (method_name == "clear_errors") {
        Module::expect(arguments, 0);
        this->clear_errors();
    } else {
        Module::call(method_name, arguments);
    }
}

void RmdMotor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    switch (data[0]) {
    case 0x60: {
        int32_t encoder = 0;
        std::memcpy(&encoder, data + 4, 4);
        this->properties.at("position")->number_value = encoder / 16384.0 * 360.0 / this->ratio; // 16384 = 2^14
        break;
    }
    case 0x30: {
        echo("%s pid %3d %3d %3d %3d %3d %3d",
             this->name.c_str(),
             data[6],
             data[7],
             data[4],
             data[5],
             data[2],
             data[3]);
        break;
    }
    case 0x42: {
        int32_t acceleration = 0;
        std::memcpy(&acceleration, data + 4, 4);
        echo("%s.acceleration %d", this->name.c_str(), acceleration);
        break;
    }
    case 0x43: {
        uint8_t index = 0;
        std::memcpy(&index, data + 1, 1);
        int32_t acceleration = 0;
        std::memcpy(&acceleration, data + 4, 4);
        echo("%s.acceleration[%d] %d", this->name.c_str(), index, acceleration);
        break;
    }
    case 0x9a: {
        int8_t temperature = 0;
        std::memcpy(&temperature, data + 1, 1);
        uint16_t voltage = 0;
        std::memcpy(&voltage, data + 4, 2);
        uint16_t errors = 0;
        std::memcpy(&errors, data + 6, 2);
        echo("%s.status %d %.1f %d", this->name.c_str(), temperature, (float)voltage / 10.0, errors);
        break;
    }
    }
    this->last_msg_id = data[0];
    this->last_msg_millis = millis();
}

double RmdMotor::get_position() const {
    return this->properties.at("position")->number_value;
}

double RmdMotor::get_speed() const {
    return this->properties.at("speed")->number_value;
}

void RmdMotor::set_acceleration(const uint8_t index, const uint32_t acceleration) {
    this->send(0x43,
               index,
               0,
               0,
               *((uint8_t *)(&acceleration) + 0),
               *((uint8_t *)(&acceleration) + 1),
               *((uint8_t *)(&acceleration) + 2),
               *((uint8_t *)(&acceleration) + 3),
               20);
}
