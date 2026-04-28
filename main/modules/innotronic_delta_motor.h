#pragma once

#include "innotronic_motor_base.h"

class InnotronicDeltaMotor;
using InnotronicDeltaMotor_ptr = std::shared_ptr<InnotronicDeltaMotor>;

static constexpr uint8_t REF_OK = 1;
static constexpr uint8_t REF_OVERCURRENT = 2;
static constexpr uint8_t REF_END = 4;

class InnotronicDeltaMotor : public InnotronicMotorBase {
public:
    struct MotorConfig {
        int ticks;
        uint16_t mode; // operating mode word for Configure 0x0B / setting 0x02
    };

    const int motor_ticks;

private:
    bool is_reversed() const;
    double sign() const;

    void send_rel_angle_cmd(float angle, uint16_t vel_limit = 0xFFFF, uint8_t acc_limit = 0x00, int8_t jerk_limit_exp = 0x00);

    static MotorConfig config_for(const std::string &motor_type);

public:
    InnotronicDeltaMotor(const std::string name, const Can_ptr can, const uint32_t node_id,
                         const std::string motor_type);
    void subscribe_to_can();
    void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

    void send_delta_angle_cmd(uint8_t motor_select, int16_t pos1, uint8_t spd1 = 10, int16_t pos2 = 0, uint8_t spd2 = 10);
    void send_single_motor_control(uint8_t cmd_motor1, uint8_t cmd_motor2);
    void send_reference_drive(uint8_t motor, uint8_t cmd);
    void reference_drive_start(uint8_t motor, bool clockwise = true);
    void reference_drive_stop(uint8_t motor);
    void stop();
};
