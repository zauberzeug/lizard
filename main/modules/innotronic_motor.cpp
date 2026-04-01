#include "innotronic_motor.h"
#include "../utils/uart.h"
#include <cmath>
#include <cstring>
#include <memory>

REGISTER_MODULE_DEFAULTS(InnotronicMotor)

// add hard limit # tested with speed (10) -> ~8.62 || 8.00 with tracks installed
// bypass limit with debug porperty
// debug property should also add the currently active debug outputs

const std::map<std::string, Variable_ptr> InnotronicMotor::get_defaults() {
    return {
        {"angular_vel", std::make_shared<NumberVariable>()},
        {"current", std::make_shared<NumberVariable>()},
        {"temperature", std::make_shared<IntegerVariable>()},
        {"state", std::make_shared<IntegerVariable>()},
        {"error_codes", std::make_shared<StringVariable>("0x0000")},
        {"angle", std::make_shared<NumberVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
        {"m_per_rad", std::make_shared<NumberVariable>(1.0)},
        {"reversed", std::make_shared<BooleanVariable>(false)},
        {"rad_limit", std::make_shared<NumberVariable>(8.0)},
        {"debug", std::make_shared<BooleanVariable>(false)},
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
        // 0.01 rad/s per bit; 6.28 equals to 1 rotation per second
        double sign = this->reversed ? -1.0 : 1.0;
        this->properties.at("angular_vel")->number_value = raw_vel * 0.01 * sign;

        int16_t raw_current;
        std::memcpy(&raw_current, data + 2, 2);
        // 0.095 A per bit
        this->properties.at("current")->number_value = raw_current * 0.095;

        // Temperature in degrees Celsius
        this->properties.at("temperature")->integer_value = static_cast<int8_t>(data[4]);
        this->properties.at("state")->integer_value = data[5];

        uint16_t raw_error;
        std::memcpy(&raw_error, data + 6, 2);
        char hex_buf[7];
        snprintf(hex_buf, sizeof(hex_buf), "0x%04x", raw_error);
        this->properties.at("error_codes")->string_value = hex_buf;
        break;
    }
    case 0x12: {
        int16_t raw_angle;
        std::memcpy(&raw_angle, data, 2);
        double sign = this->reversed ? -1.0 : 1.0;
        this->properties.at("angle")->number_value = raw_angle * (M_PI / 9000.0) * sign;
        break;
    }
    }
}

void InnotronicMotor::send_speed_cmd(float angular_vel, uint8_t acc_limit, int8_t jerk_limit_exp) {
    if (!this->enabled) {
        return;
    }
    float rad_limit = this->properties.at("rad_limit")->number_value;
    if (angular_vel > rad_limit) {
        angular_vel = rad_limit;
    } else if (angular_vel < -rad_limit) {
        angular_vel = -rad_limit;
    }
    float sign = this->reversed ? -1.0f : 1.0f;
    int16_t raw_vel = static_cast<int16_t>(angular_vel * sign / 0.01);
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(data, &raw_vel, 2);
    data[2] = acc_limit;
    data[3] = static_cast<uint8_t>(jerk_limit_exp);
    uint32_t can_id = (this->node_id << 5) | 0x01;
    echo("CAN TX [NodeID=%ld, CmdID=0x01]: 0x%03lx: %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x (speed %.2f rad/s, raw %d)",
         this->node_id, can_id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], angular_vel, raw_vel);
    this->can->send(can_id, data);
}

void InnotronicMotor::send_rel_angle_cmd(float angle, uint16_t vel_limit, uint8_t acc_limit, int8_t jerk_limit_exp) {
    if (!this->enabled) {
        return;
    }
    float sign = this->reversed ? -1.0f : 1.0f;
    int16_t raw_angle = static_cast<int16_t>(angle * sign / 0.001);
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(data, &raw_angle, 2);
    std::memcpy(data + 2, &vel_limit, 2);
    data[4] = acc_limit;
    data[5] = static_cast<uint8_t>(jerk_limit_exp);
    uint32_t can_id = (this->node_id << 5) | 0x02;
    echo("CAN TX [NodeID=%ld, CmdID=0x02]: 0x%03lx: %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x (rel_angle %.3f rad, raw %d)",
         this->node_id, can_id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], angle, raw_angle);
    this->can->send(can_id, data);
}

void InnotronicMotor::send_delta_angle_cmd(float angle_a, float angle_b, uint8_t vel_lim_a, uint8_t vel_lim_b, uint8_t acc_lim, int8_t jerk_lim_exp) {
    if (!this->enabled) {
        return;
    }
    // AngleCmd CmdID 0x03: AngleA(0-1) + VelLimA(2) + AngleB(3-4) + VelLimB(5) + AccLim(6) + JerkLimExp(7)
    // Angle unit: π/9000 rad per bit (= 0.02°)
    // VelLim unit: π/64 rad/s per bit
    int16_t raw_angle_a = static_cast<int16_t>(angle_a / (M_PI / 9000.0));
    int16_t raw_angle_b = static_cast<int16_t>(angle_b / (M_PI / 9000.0));
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(data, &raw_angle_a, 2);
    data[2] = vel_lim_a;
    std::memcpy(data + 3, &raw_angle_b, 2);
    data[5] = vel_lim_b;
    data[6] = acc_lim;
    data[7] = static_cast<uint8_t>(jerk_lim_exp);
    uint32_t can_id = (this->node_id << 5) | 0x03;
    echo("CAN TX [NodeID=%ld, CmdID=0x03]: 0x%03lx: %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x (delta_angle A=%.3f B=%.3f rad)",
         this->node_id, can_id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], angle_a, angle_b);
    this->can->send(can_id, data);
}

void InnotronicMotor::send_switch_state(uint8_t state) {
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    data[0] = state;
    uint32_t can_id = (this->node_id << 5) | 0x0A;
    echo("CAN TX [NodeID=%ld, CmdID=0x0a]: 0x%03lx: %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x (switch_state %d)",
         this->node_id, can_id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], state);
    this->can->send(can_id, data);
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
        uint16_t vel_limit = arguments.size() > 1 ? static_cast<uint16_t>(arguments[1]->evaluate_number() / 0.01) : 0xFFFF;
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
        uint32_t can_id = (this->node_id << 5) | 0x0B;
        echo("CAN TX [NodeID=%ld, CmdID=0x0b]: 0x%03lx: %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x (configure sid=%d v1=%d v2=%ld)",
             this->node_id, can_id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], setting_id, value1, value2);
        this->can->send(can_id, data);
    } else if (method_name == "delta_angle") {
        if (arguments.size() < 2 || arguments.size() > 6) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, numbery, numbery, numbery, numbery, numbery, numbery);
        float angle_a = arguments[0]->evaluate_number();
        float angle_b = arguments[1]->evaluate_number();
        uint8_t vel_lim_a = arguments.size() > 2 ? static_cast<uint8_t>(arguments[2]->evaluate_number()) : 0xFF;
        uint8_t vel_lim_b = arguments.size() > 3 ? static_cast<uint8_t>(arguments[3]->evaluate_number()) : 0xFF;
        uint8_t acc_lim = arguments.size() > 4 ? static_cast<uint8_t>(arguments[4]->evaluate_number()) : 0xFF;
        int8_t jerk_lim_exp = arguments.size() > 5 ? static_cast<int8_t>(arguments[5]->evaluate_number()) : (int8_t)0xFF;
        this->send_delta_angle_cmd(angle_a, angle_b, vel_lim_a, vel_lim_b, acc_lim, jerk_lim_exp);
    } else if (method_name == "switch_to_delta_mode") {
        Module::expect(arguments, 0);
        // #PLACEHOLDER - configure setting_id=4, values TBD from Innotronic
        uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        data[0] = 0x04; // #PLACEHOLDER setting_id
        uint32_t can_id = (this->node_id << 5) | 0x0B;
        echo("CAN TX [NodeID=%ld, CmdID=0x0b]: 0x%03lx: switch_to_delta_mode (PLACEHOLDER)",
             this->node_id, can_id);
        this->can->send(can_id, data);
    } else if (method_name == "request_angle") {
        Module::expect(arguments, 0);
        uint8_t empty[8] = {};
        this->can->send((this->node_id << 5) | 0x12, empty, false, 0);
    } else if (method_name == "scan") {
        Module::expect(arguments, 0);
        echo("Scanning all CAN IDs 0x001-0x7FF...");
        uint8_t data[8] = {0xe8, 0x03, 0, 0, 0, 0, 0, 0};
        for (uint32_t can_id = 0x001; can_id <= 0x7FF; ++can_id) {
            echo("Sending to CAN ID 0x%03lx", can_id);
            this->can->send(can_id, data);
        }
        echo("Scan complete.");
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->send_switch_state(1);
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->stop();
    } else if (method_name == "on") {
        Module::expect(arguments, 0);
        this->send_switch_state(3);
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
    this->send_switch_state(2);
}

double InnotronicMotor::get_position() {
    double m_per_rad = this->properties.at("m_per_rad")->number_value;
    return this->properties.at("angle")->number_value * m_per_rad;
}

void InnotronicMotor::position(const double position, const double speed, const double acceleration) {
    double m_per_rad = this->properties.at("m_per_rad")->number_value;
    float rel = static_cast<float>((position - this->get_position()) / m_per_rad);
    uint16_t vel_limit = speed > 0 ? static_cast<uint16_t>(speed / m_per_rad / 0.01) : 0xFFFF;
    uint8_t acc_limit = acceleration > 0 ? static_cast<uint8_t>(acceleration) : 0xFF;
    this->send_rel_angle_cmd(rel, vel_limit, acc_limit);
}

double InnotronicMotor::get_speed() {
    double m_per_rad = this->properties.at("m_per_rad")->number_value;
    return this->properties.at("angular_vel")->number_value * m_per_rad;
}

void InnotronicMotor::speed(const double speed, const double acceleration) {
    double m_per_rad = this->properties.at("m_per_rad")->number_value;
    float angular_vel = static_cast<float>(speed / m_per_rad);
    uint8_t acc_limit = acceleration > 0 ? static_cast<uint8_t>(acceleration) : 0xFF;
    this->send_speed_cmd(angular_vel, acc_limit);
}

void InnotronicMotor::enable() {
    this->enabled = true;
    this->properties.at("enabled")->boolean_value = true;
    this->send_switch_state(3);
}

void InnotronicMotor::disable() {
    this->send_switch_state(1);
    this->enabled = false;
    this->properties.at("enabled")->boolean_value = false;
}
