#include "innotronic_drive_motor.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include <cmath>
#include <cstring>
#include <stdexcept>

REGISTER_MODULE_DEFAULTS(InnotronicDriveMotor)

const std::map<std::string, Variable_ptr> InnotronicDriveMotor::get_defaults() {
    auto defaults = InnotronicMotorBase::common_defaults();
    defaults["speed"] = std::make_shared<NumberVariable>();
    defaults["m_per_rad"] = std::make_shared<NumberVariable>(1.0);
    defaults["reversed"] = std::make_shared<BooleanVariable>(false);
    defaults["rad_limit"] = std::make_shared<NumberVariable>(7.8);
    defaults["current_m1"] = std::make_shared<NumberVariable>();
    defaults["current_m2"] = std::make_shared<NumberVariable>();
    return defaults;
}

InnotronicDriveMotor::InnotronicDriveMotor(const std::string name, const Can_ptr can, const uint32_t node_id)
    : InnotronicMotorBase(innotronic_drive_motor, name, can, node_id) {
    this->properties = InnotronicDriveMotor::get_defaults();
    // Operating mode 0xA5A5 = drive mode (per firmware spec). Fire-and-forget at construction.
    this->configure(0x02, 0xA5A5, 0);
}

bool InnotronicDriveMotor::is_reversed() const {
    return this->properties.at("reversed")->boolean_value;
}

double InnotronicDriveMotor::sign() const {
    return this->is_reversed() ? -1.0 : 1.0;
}

void InnotronicDriveMotor::subscribe_to_can() {
    auto self = std::static_pointer_cast<Module>(this->shared_from_this());
    this->can->subscribe((this->node_id << 5) | 0x11, self);
    this->can->subscribe((this->node_id << 5) | 0x12, self);
}

void InnotronicDriveMotor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    uint8_t cmd_id = id & 0x1F;
    switch (cmd_id) {
    case 0x11:
        this->handle_status_msg(data);
        break;
    case 0x12: {
        // DriveStatus (~100ms): motor 1 velocity, relative position counter, per-motor currents.
        // Bytes: [0-1] vel_m1 int16 0.01 rad/s, [2-3] position int16 hall ticks (wraps at ±32768),
        //        [4-5] current_m1 int16 mA, [6-7] current_m2 int16 mA
        int16_t raw_vel_m1;
        std::memcpy(&raw_vel_m1, data, 2);
        const double m_per_rad = this->properties.at("m_per_rad")->number_value;
        this->properties.at("speed")->number_value = raw_vel_m1 * 0.01 * m_per_rad * this->sign();

        int16_t raw_position;
        std::memcpy(&raw_position, data + 2, 2);
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

        int16_t raw_current_m1, raw_current_m2;
        std::memcpy(&raw_current_m1, data + 4, 2);
        std::memcpy(&raw_current_m2, data + 6, 2);
        this->properties.at("current_m1")->number_value = raw_current_m1 * 0.001;
        this->properties.at("current_m2")->number_value = raw_current_m2 * 0.001;

        if (this->is_debug()) {
            echo("[%lu] CAN RX [NodeID=%ld, CmdID=0x12]: speed %.2f m/s, position %d ticks, I_m1 %.3f A, I_m2 %.3f A",
                 millis(), this->node_id,
                 this->properties.at("speed")->number_value,
                 raw_position,
                 this->properties.at("current_m1")->number_value,
                 this->properties.at("current_m2")->number_value);
        }
        break;
    }
    }
}

void InnotronicDriveMotor::send_speed_cmd(float angular_vel, uint8_t acc_limit, int8_t jerk_limit_exp) {
    // SpeedCmd 0x01: target speed.
    // Bytes: [0-1] vel int16 0.01 rad/s, [2] acc_limit (0=default), [3] jerk_limit_exp (0=default)
    if (!this->is_enabled()) {
        return;
    }
    const float rad_limit = this->properties.at("rad_limit")->number_value;
    if (angular_vel > rad_limit) {
        angular_vel = rad_limit;
    } else if (angular_vel < -rad_limit) {
        angular_vel = -rad_limit;
    }
    int16_t raw_vel = static_cast<int16_t>(angular_vel * this->sign() / 0.01f);
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(data, &raw_vel, 2);
    data[2] = acc_limit;
    data[3] = static_cast<uint8_t>(jerk_limit_exp);
    uint32_t can_id = (this->node_id << 5) | 0x01;
    if (this->is_debug()) {
        echo("[%lu] CAN TX [NodeID=%ld, CmdID=0x01]: speed %.2f rad/s, raw %d",
             millis(), this->node_id, angular_vel, raw_vel);
    }
    this->can->send(can_id, data);
}

void InnotronicDriveMotor::send_drive_ticks_cmd(float angular_vel, int16_t ticks) {
    // DriveTicksCmd 0x04: relative move in hall ticks.
    // Bytes: [0-1] vel int16 0.01 rad/s, [2-3] ticks int16
    if (!this->is_enabled()) {
        return;
    }
    const float rad_limit = this->properties.at("rad_limit")->number_value;
    if (angular_vel > rad_limit) {
        angular_vel = rad_limit;
    } else if (angular_vel < -rad_limit) {
        angular_vel = -rad_limit;
    }
    const double s = this->sign();
    int16_t raw_vel = static_cast<int16_t>(angular_vel * s / 0.01f);
    int16_t raw_ticks = static_cast<int16_t>(ticks * s);
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(data, &raw_vel, 2);
    std::memcpy(data + 2, &raw_ticks, 2);
    uint32_t can_id = (this->node_id << 5) | 0x04;
    if (this->is_debug()) {
        echo("[%lu] CAN TX [NodeID=%ld, CmdID=0x04]: speed %.2f rad/s, ticks %d",
             millis(), this->node_id, angular_vel, raw_ticks);
    }
    this->can->send(can_id, data);
}

void InnotronicDriveMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "speed") {
        if (arguments.size() < 1 || arguments.size() > 3) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, numbery, numbery, numbery);
        float angular_vel = arguments[0]->evaluate_number();
        uint8_t acc_limit = arguments.size() > 1 ? static_cast<uint8_t>(arguments[1]->evaluate_number()) : 0x00;
        int8_t jerk_limit_exp = arguments.size() > 2 ? static_cast<int8_t>(arguments[2]->evaluate_number()) : (int8_t)0x00;
        this->send_speed_cmd(angular_vel, acc_limit, jerk_limit_exp);
    } else if (method_name == "drive_ticks") {
        Module::expect(arguments, 2, numbery, integer);
        float angular_vel = arguments[0]->evaluate_number();
        int16_t ticks = static_cast<int16_t>(arguments[1]->evaluate_integer());
        this->send_drive_ticks_cmd(angular_vel, ticks);
    } else {
        InnotronicMotorBase::call(method_name, arguments);
    }
}

void InnotronicDriveMotor::stop() {
    this->send_switch_state(2);
}

double InnotronicDriveMotor::get_position() {
    // The int16 hall counter from 0x12 wraps at ±32768; we unfold it into
    // accumulated_ticks on each frame. Convert to meters via motor_ticks and m_per_rad.
    const double m_per_rad = this->properties.at("m_per_rad")->number_value;
    return static_cast<double>(this->accumulated_ticks) / MOTOR_TICKS * 2.0 * M_PI * m_per_rad * this->sign();
}

void InnotronicDriveMotor::position(const double position, const double speed, const double acceleration) {
    // Not supported: drive mode has no closed-loop position control.
}

double InnotronicDriveMotor::get_speed() {
    return this->properties.at("speed")->number_value;
}

void InnotronicDriveMotor::speed(const double speed, const double acceleration) {
    const double m_per_rad = this->properties.at("m_per_rad")->number_value;
    float angular_vel = static_cast<float>(speed / m_per_rad);
    uint8_t acc_limit = acceleration > 0 ? static_cast<uint8_t>(acceleration) : 0xFF;
    this->send_speed_cmd(angular_vel, acc_limit);
}

void InnotronicDriveMotor::enable() {
    InnotronicMotorBase::enable();
}

void InnotronicDriveMotor::disable() {
    InnotronicMotorBase::disable();
}
