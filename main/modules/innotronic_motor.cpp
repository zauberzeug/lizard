#include "innotronic_motor.h"
#include "../utils/uart.h"
#include <cmath>
#include <cstring>
#include <memory>

REGISTER_MODULE_DEFAULTS(InnotronicMotor)

const std::map<std::string, Variable_ptr> InnotronicMotor::get_defaults() {
    return {
        {"angular_vel", std::make_shared<NumberVariable>()},
        {"current", std::make_shared<NumberVariable>()},
        {"temperature", std::make_shared<IntegerVariable>()},
        {"state", std::make_shared<IntegerVariable>()},
        {"error_codes", std::make_shared<IntegerVariable>()},
        {"angle", std::make_shared<NumberVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
        {"m_per_rad", std::make_shared<NumberVariable>(1.0)},
        {"reversed", std::make_shared<BooleanVariable>(false)},
    };
}

InnotronicMotor::InnotronicMotor(const std::string name, const Can_ptr can, const uint32_t node_id)
    : Module(innotronic_motor, name), node_id(node_id), can(can) {
    this->properties = InnotronicMotor::get_defaults();
}

void InnotronicMotor::subscribe_to_can() {
    this->can->subscribe((this->node_id << 5) | 0x11, std::static_pointer_cast<Module>(this->shared_from_this()));
    this->can->subscribe((this->node_id << 5) | 0x12, std::static_pointer_cast<Module>(this->shared_from_this()));
}

void InnotronicMotor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    uint8_t cmd_id = id & 0x1F;
    switch (cmd_id) {
    case 0x11: {
        int16_t raw_vel;
        std::memcpy(&raw_vel, data, 2);
        this->properties.at("angular_vel")->number_value = raw_vel * 0.001;

        int16_t raw_current;
        std::memcpy(&raw_current, data + 2, 2);
        this->properties.at("current")->number_value = raw_current * 0.01;

        this->properties.at("temperature")->integer_value = data[4];
        this->properties.at("state")->integer_value = data[5];

        uint16_t raw_error;
        std::memcpy(&raw_error, data + 6, 2);
        this->properties.at("error_codes")->integer_value = raw_error;
        break;
    }
    case 0x12: {
        int16_t raw_angle;
        std::memcpy(&raw_angle, data, 2);
        this->properties.at("angle")->number_value = raw_angle * (M_PI / 9000.0);
        break;
    }
    }
}

void InnotronicMotor::send_speed_cmd(float angular_vel, uint8_t acc_limit, int8_t jerk_limit_exp) {
    if (!this->enabled) {
        return;
    }
    int16_t raw_vel = static_cast<int16_t>(angular_vel / 0.001);
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(data, &raw_vel, 2);
    data[2] = acc_limit;
    data[3] = static_cast<uint8_t>(jerk_limit_exp);
    this->can->send((this->node_id << 5) | 0x01, data);
}

void InnotronicMotor::send_rel_angle_cmd(float angle, uint16_t vel_limit, uint8_t acc_limit, int8_t jerk_limit_exp) {
    if (!this->enabled) {
        return;
    }
    int16_t raw_angle = static_cast<int16_t>(angle / 0.001);
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(data, &raw_angle, 2);
    std::memcpy(data + 2, &vel_limit, 2);
    data[4] = acc_limit;
    data[5] = static_cast<uint8_t>(jerk_limit_exp);
    this->can->send((this->node_id << 5) | 0x02, data);
}

void InnotronicMotor::send_switch_state(uint8_t state) {
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    data[0] = state;
    this->can->send((this->node_id << 5) | 0x0A, data);
}

void InnotronicMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "speed") {
        if (arguments.size() < 1 || arguments.size() > 3) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, numbery, numbery, numbery);
        float angular_vel = arguments[0]->evaluate_number();
        uint8_t acc_limit = arguments.size() > 1 ? static_cast<uint8_t>(arguments[1]->evaluate_number()) : 0xFF;
        int8_t jerk_limit_exp = arguments.size() > 2 ? static_cast<int8_t>(arguments[2]->evaluate_number()) : (int8_t)0xFF;
        this->send_speed_cmd(angular_vel, acc_limit, jerk_limit_exp);
    } else if (method_name == "rel_angle") {
        if (arguments.size() < 1 || arguments.size() > 4) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, numbery, numbery, numbery, numbery);
        float angle = arguments[0]->evaluate_number();
        uint16_t vel_limit = arguments.size() > 1 ? static_cast<uint16_t>(arguments[1]->evaluate_number() / 0.001) : 0xFFFF;
        uint8_t acc_limit = arguments.size() > 2 ? static_cast<uint8_t>(arguments[2]->evaluate_number()) : 0xFF;
        int8_t jerk_limit_exp = arguments.size() > 3 ? static_cast<int8_t>(arguments[3]->evaluate_number()) : (int8_t)0xFF;
        this->send_rel_angle_cmd(angle, vel_limit, acc_limit, jerk_limit_exp);
    } else if (method_name == "switch_state") {
        Module::expect(arguments, 1, integer);
        this->send_switch_state(static_cast<uint8_t>(arguments[0]->evaluate_integer()));
    } else if (method_name == "configure") {
        Module::expect(arguments, 3, integer, integer, integer);
        uint8_t setting_id = static_cast<uint8_t>(arguments[0]->evaluate_integer());
        uint16_t value1 = static_cast<uint16_t>(arguments[1]->evaluate_integer());
        int32_t value2 = static_cast<int32_t>(arguments[2]->evaluate_integer());
        uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        data[0] = setting_id;
        std::memcpy(data + 2, &value1, 2);
        std::memcpy(data + 4, &value2, 4);
        this->can->send((this->node_id << 5) | 0x0B, data);
    } else if (method_name == "scan") {
        Module::expect(arguments, 0);
        echo("Scanning for Innotronic motors on CAN bus...");
        uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        for (uint32_t id = 1; id <= 31; ++id) {
            this->can->send((id << 5) | 0x0A, data, false, 1);
        }
        echo("Scan sent to node IDs 1-31. Enable CAN output to see responses.");
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->send_switch_state(0);
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->stop();
    } else if (method_name == "on") {
        Module::expect(arguments, 0);
        this->send_switch_state(2);
    } else if (method_name == "enable") {
        Module::expect(arguments, 0);
        this->enable();
    } else if (method_name == "disable") {
        Module::expect(arguments, 0);
        this->disable();
    } else {
        Module::call(method_name, arguments);
    }
}

void InnotronicMotor::step() {
    if (this->properties.at("enabled")->boolean_value != this->enabled) {
        if (this->properties.at("enabled")->boolean_value) {
            this->enable();
        } else {
            this->disable();
        }
    }
    this->reversed = this->properties.at("reversed")->boolean_value;
    Module::step();
}

void InnotronicMotor::stop() {
    this->send_switch_state(1);
}

double InnotronicMotor::get_position() {
    double m_per_rad = this->properties.at("m_per_rad")->number_value;
    double sign = this->reversed ? -1.0 : 1.0;
    return this->properties.at("angle")->number_value * sign * m_per_rad;
}

void InnotronicMotor::position(const double position, const double speed, const double acceleration) {
    double m_per_rad = this->properties.at("m_per_rad")->number_value;
    double sign = this->reversed ? -1.0 : 1.0;
    float rel = static_cast<float>((position - this->get_position()) / m_per_rad * sign);
    uint16_t vel_limit = speed > 0 ? static_cast<uint16_t>(speed / m_per_rad / 0.001) : 0xFFFF;
    uint8_t acc_limit = acceleration > 0 ? static_cast<uint8_t>(acceleration) : 0xFF;
    this->send_rel_angle_cmd(rel, vel_limit, acc_limit);
}

double InnotronicMotor::get_speed() {
    double m_per_rad = this->properties.at("m_per_rad")->number_value;
    double sign = this->reversed ? -1.0 : 1.0;
    return this->properties.at("angular_vel")->number_value * sign * m_per_rad;
}

void InnotronicMotor::speed(const double speed, const double acceleration) {
    double m_per_rad = this->properties.at("m_per_rad")->number_value;
    double sign = this->reversed ? -1.0 : 1.0;
    float angular_vel = static_cast<float>(speed / m_per_rad * sign);
    uint8_t acc_limit = acceleration > 0 ? static_cast<uint8_t>(acceleration) : 0xFF;
    this->send_speed_cmd(angular_vel, acc_limit);
}

void InnotronicMotor::enable() {
    this->enabled = true;
    this->properties.at("enabled")->boolean_value = true;
    this->send_switch_state(2);
}

void InnotronicMotor::disable() {
    this->send_switch_state(0);
    this->enabled = false;
    this->properties.at("enabled")->boolean_value = false;
}
