#pragma once

#include "can.h"
#include "module.h"
#include <memory>

class MksServoMotor;
using MksServoMotor_ptr = std::shared_ptr<MksServoMotor>;

class MksServoMotor : public Module, public std::enable_shared_from_this<MksServoMotor> {
private:
    const uint8_t can_id;
    const Can_ptr can;
    unsigned long int last_msg_millis = 0;
    bool enabled = true;

    void send(const uint8_t *data, const uint8_t len) const;
    uint8_t calc_crc(const uint8_t *data, const uint8_t len) const;

public:
    MksServoMotor(const std::string name, const Can_ptr can, const uint8_t can_id);
    void subscribe_to_can();
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

    void set_mode(const uint8_t mode);
    void set_working_current(const uint16_t current_ma);
    void set_holding_current(const uint8_t ratio);
    void move_to_position(const uint16_t speed, const uint8_t acceleration, const int32_t abs_pulses);
    void run_speed(const uint8_t direction, const uint16_t speed, const uint8_t acceleration);
    void grip(const uint16_t working_current_ma, const uint8_t holding_ratio);
    void stop();
    void query_status();
    void read_encoder();
    void read_speed();
    void clear_stall();
    void set_zero();
    void home(const uint16_t working_current_ma, const uint8_t holding_ratio);
    void enable();
    void disable();
};
