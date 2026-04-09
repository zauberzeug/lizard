#include "innotronic_motor.h"
#include "../utils/uart.h"
#include <cmath>
#include <cstring>
#include <memory>

REGISTER_MODULE_DEFAULTS(InnotronicMotor)

static constexpr int DRIVE_MOTOR_TICKS = 600;
static constexpr int DELTA_MOTOR_TICKS = 200;

const std::map<std::string, Variable_ptr> InnotronicMotor::get_defaults() {
    return {
        {"voltage", std::make_shared<NumberVariable>()},
        {"angular_vel", std::make_shared<NumberVariable>()},
        {"current_m1", std::make_shared<NumberVariable>()},
        {"current_m2", std::make_shared<NumberVariable>()},
        {"angular_vel_m1", std::make_shared<NumberVariable>()},
        {"angular_vel_m2", std::make_shared<NumberVariable>()},
        {"motor_ticks", std::make_shared<IntegerVariable>(DRIVE_MOTOR_TICKS)},
        {"temperature", std::make_shared<IntegerVariable>()},
        {"state", std::make_shared<IntegerVariable>()},
        {"error_codes", std::make_shared<StringVariable>("0x0000")},
        {"angle_m1", std::make_shared<NumberVariable>()},
        {"angle_m2", std::make_shared<NumberVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
        {"m_per_rad", std::make_shared<NumberVariable>(1.0)},
        {"reversed", std::make_shared<BooleanVariable>(false)},
        {"rad_limit", std::make_shared<NumberVariable>(6.0)}, // take testet on robot currently with 600 on 02.04.2026

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
    this->can->subscribe((this->node_id << 5) | 0x13, std::static_pointer_cast<Module>(this->shared_from_this()));
}

void InnotronicMotor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    uint8_t cmd_id = id & 0x1F;
    switch (cmd_id) {
    case 0x11: {
        int16_t raw_vel;
        std::memcpy(&raw_vel, data, 2);
        this->properties.at("angular_vel")->number_value = raw_vel * 0.01;

        int16_t raw_voltage;
        std::memcpy(&raw_voltage, data + 2, 2);
        this->properties.at("voltage")->number_value = raw_voltage * 0.01;

        // Temperature in degrees Celsius
        this->properties.at("temperature")->integer_value = static_cast<int8_t>(data[4]);
        this->properties.at("state")->integer_value = data[5];

        uint16_t raw_error;
        std::memcpy(&raw_error, data + 6, 2);
        char hex_buf[7];
        snprintf(hex_buf, sizeof(hex_buf), "0x%04x", raw_error);
        this->properties.at("error_codes")->string_value = hex_buf;
        if (this->properties.at("debug")->boolean_value) {
            echo("CAN RX [NodeID=%ld, CmdID=0x11]: 0x%03lx: %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x (vel %.2f rad/s, voltage %.2f V, temp %d C, state %d, error %s)",
                 this->node_id, id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7],
                 this->properties.at("angular_vel")->number_value,
                 this->properties.at("voltage")->number_value,
                 (int)this->properties.at("temperature")->integer_value,
                 (int)this->properties.at("state")->integer_value,
                 hex_buf);
        }
        break;
    }
    case 0x12: {
        // Angle in hall ticks, convert to rad using motor_ticks (ticks per full revolution)
        int16_t raw_angle_m1;
        std::memcpy(&raw_angle_m1, data, 2);
        this->properties.at("angle_m1")->number_value = raw_angle_m1;
        int16_t raw_angle_m2;
        std::memcpy(&raw_angle_m2, data + 2, 2);
        this->properties.at("angle_m2")->number_value = raw_angle_m2;
        break;
    }
    case 0x13: {
        // byte 0-1: current motor 1 (int16), byte 2-3: current motor 2 (int16)
        // byte 4-5: angular_vel motor 1 (int16), byte 6-7: angular_vel motor 2 (int16)
        int16_t raw_current_m1;
        std::memcpy(&raw_current_m1, data, 2);
        this->properties.at("current_m1")->number_value = raw_current_m1 * 0.095;
        int16_t raw_current_m2;
        std::memcpy(&raw_current_m2, data + 2, 2);
        this->properties.at("current_m2")->number_value = raw_current_m2 * 0.095;
        int16_t raw_vel_m1;
        std::memcpy(&raw_vel_m1, data + 4, 2);
        this->properties.at("angular_vel_m1")->number_value = raw_vel_m1 * 0.01;
        int16_t raw_vel_m2;
        std::memcpy(&raw_vel_m2, data + 6, 2);
        this->properties.at("angular_vel_m2")->number_value = raw_vel_m2 * 0.01;
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

void InnotronicMotor::send_delta_angle_cmd(uint8_t motor_select, int16_t position_ticks, uint16_t speed_limit) {
    if (!this->enabled) {
        return;
    }
    // AngleCmd CmdID 0x03: per-motor position command
    // Byte 0: motor select (0x10 = left, 0x20 = right)
    // Byte 1-2: position in hall ticks (int16, ±150, 150 = 180°)
    // Byte 3-4: speed limit (uint16)
    // Byte 5-7: reserved
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    data[0] = motor_select;
    std::memcpy(data + 1, &position_ticks, 2);
    std::memcpy(data + 3, &speed_limit, 2);
    uint32_t can_id = (this->node_id << 5) | 0x03;
    echo("CAN TX [NodeID=%ld, CmdID=0x03]: 0x%03lx: %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x (angle_cmd motor=0x%02x ticks=%d speed=%d)",
         this->node_id, can_id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], motor_select, position_ticks, speed_limit);
    this->can->send(can_id, data);
}

void InnotronicMotor::send_single_motor_control(uint8_t cmd_motor1, uint8_t cmd_motor2) {
    // SingleMotorControl CmdID 0x0C
    // Byte 0: command for motor 1, Byte 1: command for motor 2
    // Commands: 0x00 = no action, 0x05 = brake, 0x10 = calibration CW, 0x20 = calibration CCW
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    data[0] = cmd_motor1;
    data[1] = cmd_motor2;
    uint32_t can_id = (this->node_id << 5) | 0x0C;
    echo("CAN TX [NodeID=%ld, CmdID=0x0c]: 0x%03lx: %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x (single_motor_ctrl m1=0x%02x m2=0x%02x)",
         this->node_id, can_id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], cmd_motor1, cmd_motor2);
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
        uint8_t acc_limit = arguments.size() > 1 ? static_cast<uint8_t>(arguments[1]->evaluate_number()) : 0x00;
        int8_t jerk_limit_exp = arguments.size() > 2 ? static_cast<int8_t>(arguments[2]->evaluate_number()) : (int8_t)0x00;
        this->send_speed_cmd(angular_vel, acc_limit, jerk_limit_exp);
    } else if (method_name == "rel_angle") {
        if (arguments.size() < 1 || arguments.size() > 4) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, numbery, numbery, numbery, numbery);
        float angle = arguments[0]->evaluate_number();
        uint16_t vel_limit = arguments.size() > 1 ? static_cast<uint16_t>(arguments[1]->evaluate_number() / 0.01) : 0xFFFF;
        uint8_t acc_limit = arguments.size() > 2 ? static_cast<uint8_t>(arguments[2]->evaluate_number()) : 0x00;
        int8_t jerk_limit_exp = arguments.size() > 3 ? static_cast<int8_t>(arguments[3]->evaluate_number()) : (int8_t)0x00;
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
        if (arguments.size() < 2 || arguments.size() > 3) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, integer, integer, integer);
        uint8_t motor_select = static_cast<uint8_t>(arguments[0]->evaluate_integer());
        int16_t position_ticks = static_cast<int16_t>(arguments[1]->evaluate_integer());
        uint16_t speed_limit = arguments.size() > 2 ? static_cast<uint16_t>(arguments[2]->evaluate_integer()) : 0xFFFF;
        this->send_delta_angle_cmd(motor_select, position_ticks, speed_limit);
    } else if (method_name == "switch_to_delta_mode") {
        Module::expect(arguments, 0);
        this->properties.at("motor_ticks")->integer_value = DELTA_MOTOR_TICKS;
        // #PLACEHOLDER - configure setting_id=4, values TBD from Innotronic
        uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        data[0] = 0x04; // #PLACEHOLDER setting_id
        uint32_t can_id = (this->node_id << 5) | 0x0B;
        echo("CAN TX [NodeID=%ld, CmdID=0x0b]: 0x%03lx: switch_to_delta_mode (motor_ticks=%d, PLACEHOLDER)",
             this->node_id, can_id, DELTA_MOTOR_TICKS);
        this->can->send(can_id, data);
    } else if (method_name == "single_motor_control") {
        Module::expect(arguments, 2, integer, integer);
        uint8_t cmd_motor1 = static_cast<uint8_t>(arguments[0]->evaluate_integer());
        uint8_t cmd_motor2 = static_cast<uint8_t>(arguments[1]->evaluate_integer());
        this->send_single_motor_control(cmd_motor1, cmd_motor2);
    } else if (method_name == "request_angle") {
        Module::expect(arguments, 0);
        uint8_t empty[8] = {};
        this->can->send((this->node_id << 5) | 0x12, empty, false, 0);
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->send_switch_state(1);
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->send_switch_state(2);
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
    int motor_ticks = this->properties.at("motor_ticks")->integer_value;
    double rad_per_tick = (2.0 * M_PI) / motor_ticks;
    return this->properties.at("angle_m1")->number_value * rad_per_tick * m_per_rad;
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
