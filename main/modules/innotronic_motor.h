#pragma once

#include "can.h"
#include "module.h"
#include "motor.h"
#include <memory>

class InnotronicMotor;
using InnotronicMotor_ptr = std::shared_ptr<InnotronicMotor>;

class InnotronicMotor : public Module, public std::enable_shared_from_this<InnotronicMotor>, virtual public Motor {
private:
    const uint32_t node_id;
    const Can_ptr can;
    bool enabled = true;
    bool reversed = false;

    void send_speed_cmd(float angular_vel, uint8_t acc_limit = 0xFF, int8_t jerk_limit_exp = (int8_t)0xFF);
    void send_rel_angle_cmd(float angle, uint16_t vel_limit = 0xFFFF, uint8_t acc_limit = 0xFF, int8_t jerk_limit_exp = (int8_t)0xFF);
    void send_switch_state(uint8_t state);

public:
    void send_delta_angle_cmd(uint8_t motor_select, int16_t position_ticks, uint16_t speed_limit = 0xFFFF);
    void send_single_motor_control(uint8_t cmd_motor1, uint8_t cmd_motor2);
    void send_reference_drive(uint8_t motor, uint8_t cmd);
    void reference_drive_start(uint8_t motor, bool clockwise = true);
    void reference_drive_stop(uint8_t motor);
    void configure(uint8_t setting_id, uint16_t value1, int32_t value2);
    void configure_node_id(uint8_t new_node_id);
    InnotronicMotor(const std::string name, const Can_ptr can, const uint32_t node_id);
    void subscribe_to_can();
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
    void step() override;

    void stop() override;
    double get_position() override;
    void position(const double position, const double speed, const double acceleration) override;
    double get_speed() override;
    double get_m1_speed();
    void speed(const double speed, const double acceleration) override;
    void enable() override;
    void disable() override;
};
