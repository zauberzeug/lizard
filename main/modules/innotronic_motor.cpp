#include "innotronic_motor.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include <cmath>
#include <cstring>
#include <memory>

REGISTER_MODULE_DEFAULTS(InnotronicMotor)

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
        {"rad_limit", std::make_shared<NumberVariable>(7.8)}, // take testet on robot currently with 600 on 02.04.2026
        {"ref_result_m1", std::make_shared<IntegerVariable>(0)},
        {"ref_result_m2", std::make_shared<IntegerVariable>(0)},
        {"version", std::make_shared<IntegerVariable>(0)},
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
    this->can->subscribe((this->node_id << 5) | 0x14, std::static_pointer_cast<Module>(this->shared_from_this()));
    this->can->subscribe((this->node_id << 5) | 0x15, std::static_pointer_cast<Module>(this->shared_from_this()));
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
        this->properties.at("voltage")->number_value = raw_voltage * 0.001;

        // Temperature in degrees Celsius
        this->properties.at("temperature")->integer_value = static_cast<int8_t>(data[4]);
        this->properties.at("state")->integer_value = data[5];
        // Byte 6: ErrorCodes bitmask, Byte 7: firmware version
        uint8_t raw_error = data[6];
        char hex_buf[7];
        snprintf(hex_buf, sizeof(hex_buf), "0x%04x", raw_error);
        this->properties.at("error_codes")->string_value = hex_buf;
        this->properties.at("version")->integer_value = data[7];
        if (this->properties.at("debug")->boolean_value) {
            echo("[%lu] CAN RX [NodeID=%ld, CmdID=0x11]: 0x%03lx: %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x (vel %.2f rad/s, voltage %.2f V, temp %d C, state %d, error %s, version %d)",
                 millis(), this->node_id, id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7],
                 this->properties.at("angular_vel")->number_value,
                 this->properties.at("voltage")->number_value,
                 (int)this->properties.at("temperature")->integer_value,
                 (int)this->properties.at("state")->integer_value,
                 hex_buf,
                 (int)data[7]);
        }
        break;
    }
    case 0x12: {
        // DriveStatus CmdID 0x12 (cyclic, ~100ms in drive mode): motor 1 speed, relative position, and per-motor current.
        // Byte 0-1: angular_vel motor 1 (int16, 0.01 rad/s)
        // Byte 2-3: current relative position (int16, hall ticks, wraps from -32768 to 32767 — no reset on speed = 0 or direction reversal)
        // Byte 4-5: current motor 1 (int16, milliamps)
        // Byte 6-7: current motor 2 (int16, milliamps)
        double sign = this->reversed ? -1.0 : 1.0;
        int16_t raw_vel_m1;
        std::memcpy(&raw_vel_m1, data, 2);
        this->properties.at("angular_vel_m1")->number_value = raw_vel_m1 * 0.01 * sign;
        int16_t raw_position;
        std::memcpy(&raw_position, data + 2, 2);
        this->properties.at("angle_m1")->number_value = raw_position;
        if (this->has_last_raw_position) {
            int32_t delta = static_cast<int32_t>(raw_position) - static_cast<int32_t>(this->last_raw_position);
            if (delta > 32767) {
                delta -= 65536;
            } else if (delta < -32768) {
                delta += 65536;
            }
            this->accumulated_ticks += delta;
        } else {
            this->has_last_raw_position = true;
        }
        this->last_raw_position = raw_position;
        int16_t raw_current_m1;
        std::memcpy(&raw_current_m1, data + 4, 2);
        this->properties.at("current_m1")->number_value = raw_current_m1 * 0.001;
        int16_t raw_current_m2;
        std::memcpy(&raw_current_m2, data + 6, 2);
        this->properties.at("current_m2")->number_value = raw_current_m2 * 0.001;
        if (this->properties.at("debug")->boolean_value) {
            echo("[%lu] CAN RX [NodeID=%ld, CmdID=0x12]: 0x%03lx: %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x (vel_m1 %.2f rad/s, position %d ticks, current_m1 %.3f A, current_m2 %.3f A)",
                 millis(), this->node_id, id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7],
                 this->properties.at("angular_vel_m1")->number_value,
                 raw_position,
                 this->properties.at("current_m1")->number_value,
                 this->properties.at("current_m2")->number_value);
        }
        break;
    }
    case 0x14: {
        // ReferenceFeedback CmdID 0x14: reference drive completion
        // Byte 0: MSB nibble = motor 1 result, LSB nibble = motor 2 result
        // Values: 0 = no result, 1 = ok, 2 = overcurrent, 4 = ref_end (motor keeps spinning)
        uint8_t ref_m1 = (data[0] >> 4) & 0x0F;
        uint8_t ref_m2 = data[0] & 0x0F;
        this->properties.at("ref_result_m1")->integer_value = ref_m1;
        this->properties.at("ref_result_m2")->integer_value = ref_m2;
        auto ref_str = [](uint8_t v) -> const char * {
            switch (v) {
            case REF_OK:
                return "OK";
            case REF_OVERCURRENT:
                return "OVERCURRENT";
            case REF_END:
                return "REF_END";
            default:
                return "NONE";
            }
        };
        echo("[%lu] CAN RX [NodeID=%ld, CmdID=0x14]: Reference Result Motor1: %s Motor2: %s",
             millis(), this->node_id, ref_str(ref_m1), ref_str(ref_m2));
        break;
    }
    case 0x15: {
        // CurrentAngleCurrent CmdID 0x15: per-motor angle and current (position controller mode)
        // Byte 0-1: angle motor 1 (int16, hall ticks)
        // Byte 2-3: angle motor 2 (int16, hall ticks)
        // Byte 4-5: current motor 1 (int16, milliamps)
        // Byte 6-7: current motor 2 (int16, milliamps)
        int16_t raw_angle_m1;
        std::memcpy(&raw_angle_m1, data, 2);
        this->properties.at("angle_m1")->number_value = raw_angle_m1;
        int16_t raw_angle_m2;
        std::memcpy(&raw_angle_m2, data + 2, 2);
        this->properties.at("angle_m2")->number_value = raw_angle_m2;
        int16_t raw_current_m1;
        std::memcpy(&raw_current_m1, data + 4, 2);
        this->properties.at("current_m1")->number_value = raw_current_m1 * 0.001;
        int16_t raw_current_m2;
        std::memcpy(&raw_current_m2, data + 6, 2);
        this->properties.at("current_m2")->number_value = raw_current_m2 * 0.001;
        if (this->properties.at("debug")->boolean_value) {
            echo("CAN RX [NodeID=%ld, CmdID=0x15]: angle_m1=%d angle_m2=%d current_m1=%.2fA current_m2=%.2fA",
                 this->node_id, raw_angle_m1, raw_angle_m2,
                 this->properties.at("current_m1")->number_value,
                 this->properties.at("current_m2")->number_value);
        }
        break;
    }
    }
}

void InnotronicMotor::send_speed_cmd(float angular_vel, uint8_t acc_limit, int8_t jerk_limit_exp) {
    // SpeedCmd CmdID 0x01: set target speed
    // Byte 0-1: angular velocity (int16, 0.01 rad/s)
    // Byte 2:   acceleration limit (uint8, 0x00 = default)
    // Byte 3:   jerk limit exponent (int8, 0x00 = default)
    // Byte 4-7: reserved
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
    if (this->properties.at("debug")->boolean_value) {
        echo("[%lu] CAN TX [NodeID=%ld, CmdID=0x01]: 0x%03lx: %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x (speed %.2f rad/s, raw %d)",
             millis(), this->node_id, can_id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], angular_vel, raw_vel);
    }
    this->can->send(can_id, data);
}

void InnotronicMotor::send_rel_angle_cmd(float angle, uint16_t vel_limit, uint8_t acc_limit, int8_t jerk_limit_exp) {
    // RelAngleCmd CmdID 0x02: move by relative angle
    // Byte 0-1: relative angle (int16, 0.001 rad)
    // Byte 2-3: velocity limit (uint16, 0.01 rad/s, 0xFFFF = no limit)
    // Byte 4:   acceleration limit (uint8, 0x00 = default)
    // Byte 5:   jerk limit exponent (int8, 0x00 = default)
    // Byte 6-7: reserved
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
    // echo("CAN TX [NodeID=%ld, CmdID=0x02]: 0x%03lx: %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x (rel_angle %.3f rad, raw %d)",
    //      this->node_id, can_id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], angle, raw_angle);
    this->can->send(can_id, data);
}

void InnotronicMotor::send_drive_ticks_cmd(float angular_vel, int16_t ticks) {
    // DriveTicksCmd CmdID 0x04: relative move in hall ticks for drive mode.
    // Byte 0-1: angular velocity (int16, 0.01 rad/s)
    // Byte 2-3: relative ticks (int16, hall ticks)
    // Byte 4-7: reserved
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
    int16_t raw_ticks = static_cast<int16_t>(ticks * sign);
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(data, &raw_vel, 2);
    std::memcpy(data + 2, &raw_ticks, 2);
    uint32_t can_id = (this->node_id << 5) | 0x04;
    if (this->properties.at("debug")->boolean_value) {
        echo("[%lu] CAN TX [NodeID=%ld, CmdID=0x04]: 0x%03lx: %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x (speed %.2f rad/s, ticks %d)",
             millis(), this->node_id, can_id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], angular_vel, raw_ticks);
    }
    this->can->send(can_id, data);
}

void InnotronicMotor::send_delta_angle_cmd(uint8_t motor_select, int16_t pos1, uint8_t spd1, int16_t pos2, uint8_t spd2) {
    if (!this->enabled) {
        return;
    }
    // AngleCmd CmdID 0x03: per-motor position command
    // Byte 0: motor select (0x10 = left, 0x20 = right, 0x30 = both)
    // Byte 1-2: position in hall ticks (int16, ±150, 150 = 180°)
    // Byte 3: speed limit (uint8, 1-50, 1 = fast, 50 = slow)
    // Byte 4-5: position for motor 2 if motor_select is 0x30 (int16, ±150, 150 = 180°)
    // Byte 6: speed limit for motor 2 if motor_select is 0x30
    // Byte 7: reserved
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    data[0] = motor_select;
    std::memcpy(data + 1, &pos1, 2);
    data[3] = spd1;
    if (motor_select == 0x30) {
        std::memcpy(data + 4, &pos2, 2);
        data[6] = spd2;
    }
    uint32_t can_id = (this->node_id << 5) | 0x03;
    // echo("CAN TX [NodeID=%ld, CmdID=0x03]: 0x%03lx: %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x (angle_cmd motor=0x%02x)",
    //      this->node_id, can_id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], motor_select);
    this->can->send(can_id, data);
}

void InnotronicMotor::send_reference_drive(uint8_t motor, uint8_t cmd) {
    // Reference drive via SingleMotorControl CmdID 0x0C
    // Byte 0: command for motor 1
    // Byte 1: command for motor 2
    // Commands: 0x00 = no action, 0x05 = brake (and if a reference drive is active, stores current position as 0-reference), 0x10 = calibration CW, 0x20 = calibration CCW
    // Byte 2-7: reserved
    uint8_t cmd_motor1 = (motor == 1) ? cmd : 0x00;
    uint8_t cmd_motor2 = (motor == 2) ? cmd : 0x00;
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    data[0] = cmd_motor1;
    data[1] = cmd_motor2;
    uint32_t can_id = (this->node_id << 5) | 0x0C;
    // echo("CAN TX [NodeID=%ld, CmdID=0x0c]: 0x%03lx: %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x (reference motor=%d cmd=0x%02x)",
    //      this->node_id, can_id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], motor, cmd);
    this->can->send(can_id, data);
}

void InnotronicMotor::reference_drive_start(uint8_t motor, bool clockwise) {
    uint8_t cmd = clockwise ? 0x10 : 0x20;
    this->send_reference_drive(motor, cmd);
}

void InnotronicMotor::reference_drive_stop(uint8_t motor) {
    if (this->properties.at("debug")->boolean_value) {
        echo("%s: stopping reference drive on motor %d", this->name.c_str(), motor);
    }
    this->send_reference_drive(motor, 0x05);
}

void InnotronicMotor::send_single_motor_control(uint8_t cmd_motor1, uint8_t cmd_motor2) {
    // SingleMotorControl CmdID 0x0C
    // Byte 0: command for motor 1, Byte 1: command for motor 2
    // Commands: 0x00 = no action, 0x05 = brake (and if a reference drive is active, stores current position as 0-reference), 0x10 = calibration CW, 0x20 = calibration CCW
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    data[0] = cmd_motor1;
    data[1] = cmd_motor2;
    uint32_t can_id = (this->node_id << 5) | 0x0C;
    this->can->send(can_id, data);
}

void InnotronicMotor::configure(uint8_t setting_id, uint16_t value1, int32_t value2) {
    // Configure CmdID 0x0B: send a setting to the motor controller
    // Byte 0: setting_id (0x00=ACK, 0x01=Set CanID, 0x02=Switch to Delta Arm Motor)
    // Byte 2-3: value1 (uint16, little-endian)
    // Byte 4-7: value2 (int32, little-endian)
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    data[0] = setting_id;
    std::memcpy(data + 2, &value1, 2);
    std::memcpy(data + 4, &value2, 4);
    uint32_t can_id = (this->node_id << 5) | 0x0B;
    // echo("CAN TX [NodeID=%ld, CmdID=0x0b]: 0x%03lx: %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x (configure sid=%d v1=%d v2=%ld)",
    //      this->node_id, can_id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], setting_id, value1, value2);
    this->can->send(can_id, data);
}

void InnotronicMotor::configure_node_id(uint8_t new_node_id) {
    // Set the CAN node ID of the motor controller (setting_id=0x01)
    // new_node_id is shifted left by 5 to form the CAN base address (e.g. node_id=1 -> base=0x020)
    uint16_t can_base_address = static_cast<uint16_t>(new_node_id) << 5;
    this->configure(0x01, can_base_address, 0);
}

void InnotronicMotor::send_switch_state(uint8_t state) {
    // SwitchState CmdID 0x0A: change operating state
    // Byte 0: state (1 = off, 2 = stop/brake, 3 = on)
    // Byte 1-7: reserved
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    data[0] = state;
    uint32_t can_id = (this->node_id << 5) | 0x0A;
    // echo("CAN TX [NodeID=%ld, CmdID=0x0a]: 0x%03lx: %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x (switch_state %d)",
    //      this->node_id, can_id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], state);
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
    } else if (method_name == "drive_ticks") {
        // drive_ticks(speed, ticks): relative drive command in hall ticks
        Module::expect(arguments, 2, numbery, integer);
        float angular_vel = arguments[0]->evaluate_number();
        int16_t ticks = static_cast<int16_t>(arguments[1]->evaluate_integer());
        this->send_drive_ticks_cmd(angular_vel, ticks);
    } else if (method_name == "configure_node_id") {
        // arg0: new node ID (0-63)
        Module::expect(arguments, 1, integer);
        this->configure_node_id(static_cast<uint8_t>(arguments[0]->evaluate_integer()));
    } else if (method_name == "switch_state") {
        Module::expect(arguments, 1, integer);
        this->send_switch_state(static_cast<uint8_t>(arguments[0]->evaluate_integer()));
    } else if (method_name == "configure") {
        Module::expect(arguments, 3, integer, integer, integer);
        uint8_t setting_id = static_cast<uint8_t>(arguments[0]->evaluate_integer());
        uint16_t value1 = static_cast<uint16_t>(arguments[1]->evaluate_integer());
        int32_t value2 = static_cast<int32_t>(arguments[2]->evaluate_integer());
        this->configure(setting_id, value1, value2);
    } else if (method_name == "delta_angle") {
        // delta_angle(motor_select, pos1, [speed1], [pos2], [speed2])
        // motor_select: 0x10 = left only, 0x20 = right only, 0x30 = both
        if (arguments.size() < 2 || arguments.size() > 5) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, integer, integer, integer, integer, integer);
        uint8_t motor_select = static_cast<uint8_t>(arguments[0]->evaluate_integer());
        int16_t pos1 = static_cast<int16_t>(arguments[1]->evaluate_integer());
        uint8_t spd1 = arguments.size() > 2 ? static_cast<uint8_t>(arguments[2]->evaluate_integer()) : 10;
        int16_t pos2 = arguments.size() > 3 ? static_cast<int16_t>(arguments[3]->evaluate_integer()) : 0;
        uint8_t spd2 = arguments.size() > 4 ? static_cast<uint8_t>(arguments[4]->evaluate_integer()) : 10;
        this->send_delta_angle_cmd(motor_select, pos1, spd1, pos2, spd2);
    } else if (method_name == "switch_to_delta_mode") {
        Module::expect(arguments, 0);
        this->properties.at("motor_ticks")->integer_value = DELTA_MOTOR_TICKS;
        this->configure(0x02, 0xB5B5, 0);
    } else if (method_name == "switch_to_drive_mode") {
        Module::expect(arguments, 0);
        this->properties.at("motor_ticks")->integer_value = DRIVE_MOTOR_TICKS;
        this->configure(0x02, 0xA5A5, 0);
    } else if (method_name == "single_motor_control") {
        Module::expect(arguments, 2, integer, integer);
        uint8_t cmd_motor1 = static_cast<uint8_t>(arguments[0]->evaluate_integer());
        uint8_t cmd_motor2 = static_cast<uint8_t>(arguments[1]->evaluate_integer());
        this->send_single_motor_control(cmd_motor1, cmd_motor2);
    } else if (method_name == "reference_drive_start") {
        if (arguments.size() < 1 || arguments.size() > 2) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, integer, boolean);
        uint8_t motor = static_cast<uint8_t>(arguments[0]->evaluate_integer());
        bool clockwise = arguments.size() > 1 ? arguments[1]->evaluate_boolean() : true;
        this->reference_drive_start(motor, clockwise);
    } else if (method_name == "reference_drive_stop") {
        Module::expect(arguments, 1, integer);
        uint8_t motor = static_cast<uint8_t>(arguments[0]->evaluate_integer());
        this->reference_drive_stop(motor);
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
    // Drive mode: 0x12 bytes 2-3 are an int16 hall-tick counter that wraps at +-32768.
    // Wraparound is unfolded into accumulated_ticks on each 0x12 frame; convert to meters via motor_ticks and m_per_rad.
    double motor_ticks = this->properties.at("motor_ticks")->integer_value;
    double m_per_rad = this->properties.at("m_per_rad")->number_value;
    double sign = this->reversed ? -1.0 : 1.0;
    return static_cast<double>(this->accumulated_ticks) / motor_ticks * 2.0 * M_PI * m_per_rad * sign;
}

void InnotronicMotor::position(const double position, const double speed, const double acceleration) {
    // Not supported: no position feedback in drive mode; delta arm drives positions via InnotronicDeltaArm directly.
}

double InnotronicMotor::get_speed() {
    double m_per_rad = this->properties.at("m_per_rad")->number_value;
    double sign = this->reversed ? -1.0 : 1.0;
    return this->properties.at("angular_vel")->number_value * m_per_rad * sign;
}

double InnotronicMotor::get_m1_speed() {
    // angular_vel_m1 comes from cyclic 0x12 and already has the reversed sign applied
    double m_per_rad = this->properties.at("m_per_rad")->number_value;
    return this->properties.at("angular_vel_m1")->number_value * m_per_rad;
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
