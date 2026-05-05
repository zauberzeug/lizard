#include "dual_drive_motor.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include <cmath>
#include <cstring>
#include <stdexcept>

REGISTER_MODULE_DEFAULTS(DualDriveMotor)

const std::map<std::string, Variable_ptr> DualDriveMotor::get_defaults() {
    auto defaults = DualDriveMotorBase::common_defaults();
    defaults["speed"] = std::make_shared<NumberVariable>();
    defaults["m_per_rad"] = std::make_shared<NumberVariable>(1.0);
    defaults["reversed"] = std::make_shared<BooleanVariable>(false);
    defaults["rad_limit"] = std::make_shared<NumberVariable>(7.8);
    defaults["current_m1"] = std::make_shared<NumberVariable>();
    defaults["current_m2"] = std::make_shared<NumberVariable>();
    return defaults;
}

DualDriveMotor::DualDriveMotor(const std::string name, const Can_ptr can, const uint32_t node_id)
    : DualDriveMotorBase(dual_drive_motor, name, can, node_id) {
    this->properties = DualDriveMotor::get_defaults();
    // Operating mode 0xA5A5 = drive mode (per firmware spec). Fire-and-forget at construction.
    this->configure(0x02, 0xA5A5, 0);
}

bool DualDriveMotor::is_reversed() const {
    return this->properties.at("reversed")->boolean_value;
}

double DualDriveMotor::sign() const {
    return this->is_reversed() ? -1.0 : 1.0;
}

void DualDriveMotor::subscribe_to_can() {
    auto self = std::static_pointer_cast<Module>(this->shared_from_this());
    this->can->subscribe((this->node_id << 5) | 0x11, self);
    this->can->subscribe((this->node_id << 5) | 0x12, self);
}

void DualDriveMotor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
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

void DualDriveMotor::send_speed_cmd(float angular_vel) {
    // SpeedCmd 0x01: target speed.
    // Bytes: [0-1] vel int16 0.01 rad/s, [2] acc_limit, [3] jerk_limit_exp (acceleration not yet supported by firmware)
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
    uint32_t can_id = (this->node_id << 5) | 0x01;
    if (this->is_debug()) {
        echo("[%lu] CAN TX [NodeID=%ld, CmdID=0x01]: speed %.2f rad/s, raw %d",
             millis(), this->node_id, angular_vel, raw_vel);
    }
    this->can->send(can_id, data);
}

void DualDriveMotor::send_drive_ticks_cmd(float angular_vel, int16_t ticks) {
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

void DualDriveMotor::drive_meters(float linear_speed, float distance) {
    const float m_per_rad = this->properties.at("m_per_rad")->number_value;
    const float angular_vel = linear_speed / m_per_rad;
    const int16_t ticks = static_cast<int16_t>(distance / m_per_rad * MOTOR_TICKS / (2.0f * M_PI));
    this->send_drive_ticks_cmd(angular_vel, ticks);
}

void DualDriveMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "speed") {
        Module::expect(arguments, 1, numbery);
        float angular_vel = arguments[0]->evaluate_number();
        this->send_speed_cmd(angular_vel);
    } else if (method_name == "drive_ticks") {
        Module::expect(arguments, 2, numbery, integer);
        float angular_vel = arguments[0]->evaluate_number();
        int16_t ticks = static_cast<int16_t>(arguments[1]->evaluate_integer());
        this->send_drive_ticks_cmd(angular_vel, ticks);
    } else {
        DualDriveMotorBase::call(method_name, arguments);
    }
}

void DualDriveMotor::stop() {
    this->send_switch_state(2);
}

double DualDriveMotor::get_position() {
    // The int16 hall counter from 0x12 wraps at ±32768; we unfold it into
    // accumulated_ticks on each frame. Convert to meters via motor_ticks and m_per_rad.
    const double m_per_rad = this->properties.at("m_per_rad")->number_value;
    return static_cast<double>(this->accumulated_ticks) / MOTOR_TICKS * 2.0 * M_PI * m_per_rad * this->sign();
}

void DualDriveMotor::position(const double position, const double speed, const double acceleration) {
    // Not supported: drive mode has no closed-loop position control.
}

double DualDriveMotor::get_speed() {
    return this->properties.at("speed")->number_value;
}

void DualDriveMotor::speed(const double speed, const double acceleration) {
    // acceleration parameter currently ignored: not yet supported by firmware.
    const double m_per_rad = this->properties.at("m_per_rad")->number_value;
    float angular_vel = static_cast<float>(speed / m_per_rad);
    this->send_speed_cmd(angular_vel);
}

void DualDriveMotor::enable() {
    DualDriveMotorBase::enable();
}

void DualDriveMotor::disable() {
    DualDriveMotorBase::disable();
}
