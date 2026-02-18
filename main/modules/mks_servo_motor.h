#pragma once

#include "../utils/timing.h"
#include "can.h"
#include "module.h"
#include <memory>

enum PrecisionZeroState {
    PZ_IDLE = 0,
    PZ_FIRST_ROTATE,
    PZ_WAIT_FIRST_ROTATE,
    PZ_READ_ERROR,
    PZ_WAIT_ERROR,
    PZ_CORRECT_ROTATE,
    PZ_WAIT_CORRECT_ROTATE,
    PZ_SET_ZERO,
    PZ_WAIT_AFTER_ZERO,
    PZ_MOVE_TO_START,
    PZ_DONE,
    PZ_FAILED,
};

class MksServoMotor;
using MksServoMotor_ptr = std::shared_ptr<MksServoMotor>;

class MksServoMotor : public Module, public std::enable_shared_from_this<MksServoMotor> {
private:
    Can_ptr can;
    const uint16_t can_id;

    void send(const uint8_t *data, uint8_t len);

    // Precision zero state machine
    PrecisionZeroState pz_state = PZ_IDLE;
    unsigned long pz_phase_start = 0;
    double pz_target_degrees = 240.0;
    int64_t pz_speed = 300;
    int64_t pz_acc = 300;
    unsigned long pz_rotate_wait_ms = 2000;
    unsigned long pz_correct_wait_ms = 1000;

    // Angle error read (CAN 0x39)
    bool angle_error_read_pending = false;
    bool angle_error_read_received = false;
    int32_t angle_error_value = 0;
    unsigned long angle_error_read_sent_at = 0;
    uint8_t angle_error_read_retries = 0;

    void send_angle_error_read();

    // Private helpers
    void send_enable(bool enable);
    void send_set_vfoc();
    void send_working_current(int64_t ma);
    void send_holding_current(int64_t pct);
    void send_run_internal(int64_t direction, int64_t speed, int64_t acc);
    void send_stop_internal(int64_t acc);
    void send_rotate_counts(int32_t counts, int64_t speed, int64_t acc);
    void send_rotate(double degrees, int64_t speed, int64_t acc);
    void send_coord_zero();
    void step_precision_zero();

public:
    static constexpr int32_t COUNTS_PER_TURN = 16384;
    static constexpr double COUNTS_PER_DEG = 16384.0 / 360.0;
    static constexpr int32_t INT24_MIN = -8388608;
    static constexpr int32_t INT24_MAX = 8388607;

    MksServoMotor(const std::string name, const Can_ptr can, const uint16_t can_id);
    void subscribe_to_can();
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
