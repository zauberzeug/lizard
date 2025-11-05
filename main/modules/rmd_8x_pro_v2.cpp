#include "rmd_8x_pro_v2.h"
#include "../global.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include <cstring>
#include <math.h>
#include <memory>

REGISTER_MODULE_DEFAULTS(Rmd8xProV2)

/**
 * Unique features of the RMD 8x Pro V2:
 * - return address is 0x140
 * - no 0x60 frame
 * - 0x9c position value uses full 65536 resolution for internal motor position to represent the full 360 degree range. from 0 for 0 degrees to 65535 for 359.99 degrees.
 * - 0x92 position value is on byte 1 and not byte 4 as with other RMD motors
 *
 * Position tracking strategy:
 * - 0x92 (absolute position) is the ONLY source of position updates
 * - 0x9c is only used for telemetry (temperature, torque, speed) during motion
 * - This prevents encoder drift during fast back-and-forth movements
 */

const std::map<std::string, Variable_ptr> Rmd8xProV2::get_defaults() {
    return {
        {"position", std::make_shared<NumberVariable>()},
        {"position_motor", std::make_shared<NumberVariable>()},
        {"torque", std::make_shared<NumberVariable>()},
        {"speed", std::make_shared<NumberVariable>()},
        {"temperature", std::make_shared<NumberVariable>()},
        {"can_age", std::make_shared<NumberVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
        {"running", std::make_shared<BooleanVariable>(false)},
    };
}

Rmd8xProV2::Rmd8xProV2(const std::string name, const Can_ptr can, const uint8_t motor_id, const int ratio)
    : Module(rmd_8x_pro_v2, name), motor_id(motor_id), can(can), ratio(ratio) {
    this->properties = Rmd8xProV2::get_defaults();
}

void Rmd8xProV2::subscribe_to_can() {
    can->subscribe(this->motor_id + 0x140, std::static_pointer_cast<Module>(this->shared_from_this()));
}

bool Rmd8xProV2::send(const uint8_t d0, const uint8_t d1, const uint8_t d2, const uint8_t d3,
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
            return true;
        } else {
            echo("%s warning: CAN timeout for msg id 0x%02x (attempt %d/%d)", this->name.c_str(), d0, i + 1, max_attempts);
        }
    }
    return false;
}

void Rmd8xProV2::step() {
    this->properties.at("can_age")->number_value = millis_since(this->last_msg_millis) / 1e3;

    // Always request absolute position via 0x92
    this->send(0x92, 0, 0, 0, 0, 0, 0, 0);

    // Send one initial 0x9c to get telemetry values on startup
    if (!this->initial_telemetry_sent) {
        this->send(0x9c, 0, 0, 0, 0, 0, 0, 0);
        this->initial_telemetry_sent = true;
    }

    // Request telemetry (torque, speed, temp) only when running
    if (this->properties.at("running")->boolean_value) {
        this->send(0x9c, 0, 0, 0, 0, 0, 0, 0);

        // Check if motor reached target position
        double current = this->properties.at("position_motor")->number_value;
        if (fabs(current - this->target_position_motor) < this->position_tolerance) {
            this->properties.at("running")->boolean_value = false;
        }
    }

    if (this->properties.at("enabled")->boolean_value != this->enabled) {
        if (this->properties.at("enabled")->boolean_value) {
            this->enable();
        } else {
            this->disable();
        }
    }

    Module::step();
}

bool Rmd8xProV2::power(double target_power) {
    if (!this->enabled) {
        return false;
    }
    this->properties.at("running")->boolean_value = true;
    int16_t power = target_power * 100;
    return this->send(0xa1, 0,
                      0,
                      0,
                      *((uint8_t *)(&power) + 0),
                      *((uint8_t *)(&power) + 1),
                      0,
                      0);
}

bool Rmd8xProV2::speed(double target_speed) {
    if (!this->enabled) {
        return false;
    }
    this->properties.at("running")->boolean_value = true;
    int32_t speed = target_speed * 100;
    return this->send(0xa2, 0,
                      0,
                      0,
                      *((uint8_t *)(&speed) + 0),
                      *((uint8_t *)(&speed) + 1),
                      *((uint8_t *)(&speed) + 2),
                      *((uint8_t *)(&speed) + 3));
}

bool Rmd8xProV2::position(double target_position, double target_speed) {
    if (!this->enabled) {
        return false;
    }
    this->target_position_motor = target_position;
    this->properties.at("running")->boolean_value = true;
    int32_t position = target_position * 100;
    uint16_t speed = target_speed;
    return this->send(0xa4, 0,
                      *((uint8_t *)(&speed) + 0),
                      *((uint8_t *)(&speed) + 1),
                      *((uint8_t *)(&position) + 0),
                      *((uint8_t *)(&position) + 1),
                      *((uint8_t *)(&position) + 2),
                      *((uint8_t *)(&position) + 3));
}

bool Rmd8xProV2::stop() {
    this->properties.at("running")->boolean_value = false;
    return this->send(0x81, 0, 0, 0, 0, 0, 0, 0);
}

bool Rmd8xProV2::off() {
    this->properties.at("running")->boolean_value = false;
    this->has_encoder_position = false;
    return this->send(0x80, 0, 0, 0, 0, 0, 0, 0);
}

bool Rmd8xProV2::hold() {
    if (!this->has_encoder_position)
        return false;
    this->properties.at("running")->boolean_value = false;
    echo("%s.hold position_motor: %.3f", this->name.c_str(), this->properties.at("position_motor")->number_value);
    echo("%s.hold position: %.3f", this->name.c_str(), this->properties.at("position")->number_value);
    return this->position(this->properties.at("position_motor")->number_value);
}

bool Rmd8xProV2::clear_errors() {
    return this->send(0x76, 0, 0, 0, 0, 0, 0, 0);
}

void Rmd8xProV2::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "power") {
        Module::expect(arguments, 1, numbery);
        this->power(arguments[0]->evaluate_number());
    } else if (method_name == "speed") {
        Module::expect(arguments, 1, numbery);
        this->speed(arguments[0]->evaluate_number());
    } else if (method_name == "position") {
        if (arguments.size() == 1) {
            Module::expect(arguments, 1, numbery);
            this->position(arguments[0]->evaluate_number() * this->ratio, 0);
        } else {
            Module::expect(arguments, 2, numbery, numbery);
            this->position(arguments[0]->evaluate_number() * this->ratio, arguments[1]->evaluate_number());
        }
    } else if (method_name == "position_motor") { // Question: what's the better style?
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
    } else if (method_name == "enable") {
        Module::expect(arguments, 0);
        this->enable();
    } else if (method_name == "disable") {
        Module::expect(arguments, 0);
        this->disable();
    } else if (method_name == "reposition") {
        Module::expect(arguments, 0);
        this->send(0x92, 0, 0, 0, 0, 0, 0, 0);
    } else {
        Module::call(method_name, arguments);
    }
}

void Rmd8xProV2::enable() {
    this->enabled = true;
    this->properties.at("enabled")->boolean_value = true;
    this->send(0x9c, 0, 0, 0, 0, 0, 0, 0); // Request telemetry to get initial values
}

void Rmd8xProV2::disable() {
    this->stop();
    this->enabled = false;
    this->properties.at("enabled")->boolean_value = false;
}

void Rmd8xProV2::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    if (count < 8 || data == nullptr) {
        return;
    }

    switch (data[0]) {
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
    case 0x92: {
        // 0x92: Absolute multi-turn position - THE ONLY SOURCE OF POSITION UPDATES
        int32_t raw_angle = 0;
        std::memcpy(&raw_angle, data + 1, 4);
        double motor_degrees = 0.01 * raw_angle;

        this->properties.at("position_motor")->number_value = motor_degrees;
        this->properties.at("position")->number_value = motor_degrees / this->ratio;

        this->has_encoder_position = true;
        break;
    }
    case 0x9c: {
        // 0x9c: Telemetry only - temperature, torque, speed
        // Position from encoder is IGNORED to prevent drift
        int8_t temperature = 0;
        std::memcpy(&temperature, data + 1, 1);
        int16_t torque = 0;
        std::memcpy(&torque, data + 2, 2);
        int16_t speed_motor = 0;
        std::memcpy(&speed_motor, data + 4, 2);

        this->properties.at("temperature")->number_value = temperature;
        this->properties.at("torque")->number_value = 0.01 * torque; // A
        this->properties.at("speed")->number_value = (double)speed_motor;

        break;
    }
    }
    this->last_msg_id = data[0];
    this->last_msg_millis = millis();
}

double Rmd8xProV2::get_position() const {
    return this->properties.at("position")->number_value;
}

double Rmd8xProV2::get_speed() const {
    return this->properties.at("speed")->number_value;
}

int Rmd8xProV2::get_ratio() const {
    return this->ratio;
}

bool Rmd8xProV2::set_acceleration(const uint8_t index, const uint32_t acceleration) {
    return this->send(0x43,
                      index,
                      0,
                      0,
                      *((uint8_t *)(&acceleration) + 0),
                      *((uint8_t *)(&acceleration) + 1),
                      *((uint8_t *)(&acceleration) + 2),
                      *((uint8_t *)(&acceleration) + 3),
                      20);
}
