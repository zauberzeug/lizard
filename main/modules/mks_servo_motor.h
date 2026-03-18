#pragma once

#include "can.h"
#include "module.h"
#include <memory>

class MksServoMotor;
using MksServoMotor_ptr = std::shared_ptr<MksServoMotor>;

class MksServoMotor : public Module, public std::enable_shared_from_this<MksServoMotor> {
private:
    Can_ptr can;
    const uint16_t can_id;

    void send(const uint8_t *data, uint8_t len);
    void send_position_error_read();

    // Private helpers
    void send_enable(bool enable);
    void send_set_mode(uint8_t mode);
    void send_working_current(int64_t ma);
    void send_holding_current(int64_t pct);
    void send_speed_internal(int64_t speed, int64_t direction, int64_t acc);
    void send_stop_internal(int64_t acc);
    void send_position_counts(int32_t counts, int64_t speed, int64_t acc);
    void send_position(double degrees, int64_t speed, int64_t acc);
    void send_coord_zero();

public:
    static constexpr int32_t COUNTS_PER_TURN = 16384;
    static constexpr double COUNTS_PER_DEG = 16384.0 / 360.0;
    static constexpr double POSITION_ERROR_COUNTS_PER_TURN = 51200.0;
    static constexpr int32_t INT24_MIN = -8388608;
    static constexpr int32_t INT24_MAX = 8388607;
    static constexpr int64_t MAX_WORKING_CURRENT_MA = 3000;
    static constexpr int64_t MAX_HOLDING_RATIO = 9;
    static constexpr int64_t MAX_SPEED = 3000;
    static constexpr int64_t MAX_ACC = 255;

    MksServoMotor(const std::string name, const Can_ptr can, const uint16_t can_id);
    void subscribe_to_can();
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
