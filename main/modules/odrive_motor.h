#pragma once

#include "../utils/global_error_state.h"
#include "can.h"
#include "module.h"
#include "motor.h"
#include <memory>
#include <string_view>

struct ErrorEntry {
    ErrorCode code;
    std::string_view msg;
};

class ODriveMotor;
using ODriveMotor_ptr = std::shared_ptr<ODriveMotor>;

class ODriveMotor : public Module, public std::enable_shared_from_this<ODriveMotor>, virtual public Motor {
private:
    const uint32_t can_id;
    const Can_ptr can;
    const uint32_t version;
    bool is_boot_complete = false;
    uint8_t axis_state = -1;
    uint8_t axis_control_mode = -1;
    uint8_t axis_input_mode = -1;
    bool enabled = true;
    uint32_t last_can_message_time = 0;
    bool connection_error_reported = false;

    // Error bitmask (each bit represents an error)
    uint8_t error_bitmask_ = 0;

    void set_mode(const uint8_t state, const uint8_t control_mode = 0, const uint8_t input_mode = 0);
    void check_connection();
    void set_error(ErrorCode code);

public:
    ODriveMotor(const std::string name, const Can_ptr can, const uint32_t can_id, const uint32_t version);
    void subscribe_to_can();
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

    // Module error management
    bool has_error() const;
    std::string get_error() const;

    void power(const float torque);
    void speed(const float speed);
    void position(const float position);
    void limits(const float speed, const float current);
    void off();
    void reset_motor_error();
    void step() override;

    void stop() override;
    double get_position() override;
    void position(const double position, const double speed, const double acceleration) override;
    double get_speed() override;
    void speed(const double speed, const double acceleration) override;
    void enable() override;
    void disable() override;
};
