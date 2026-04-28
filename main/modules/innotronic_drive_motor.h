#pragma once

#include "innotronic_motor_base.h"
#include "motor.h"

class InnotronicDriveMotor;
using InnotronicDriveMotor_ptr = std::shared_ptr<InnotronicDriveMotor>;

class InnotronicDriveMotor : public InnotronicMotorBase, virtual public Motor {
private:
    // Only one drive motor type so far: g350 with 600 hall ticks per revolution.
    static constexpr int MOTOR_TICKS = 600;

    int16_t last_raw_position = 0;
    bool has_last_raw_position = false;
    int64_t accumulated_ticks = 0;

    bool is_reversed() const;
    double sign() const;

    void send_speed_cmd(float angular_vel, uint8_t acc_limit = 0x00, int8_t jerk_limit_exp = 0x00);
    void send_drive_ticks_cmd(float angular_vel, int16_t ticks);

public:
    InnotronicDriveMotor(const std::string name, const Can_ptr can, const uint32_t node_id);
    void subscribe_to_can();
    void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

    void stop() override;
    double get_position() override;
    void position(const double position, const double speed, const double acceleration) override;
    double get_speed() override;
    void speed(const double speed, const double acceleration) override;
    void enable() override;
    void disable() override;
};
