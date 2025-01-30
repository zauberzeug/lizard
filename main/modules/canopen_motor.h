#pragma once

#include "can.h"
#include "canopen.h"
#include "module.h"
#include "motor.h"
#include <cstdint>
#include <memory>

class CanOpenMotor;
using CanOpenMotor_ptr = std::shared_ptr<CanOpenMotor>;

class CanOpenMotor : public Module, public std::enable_shared_from_this<CanOpenMotor>, virtual public Motor {
    Can_ptr can;
    const uint8_t node_id;

    enum InitState init_state = WaitingForPreoperational;

    /* What the motor says it's in (currently not regularly polled) */
    uint16_t current_op_mode_disp;

    /* What we last requested */
    uint16_t current_op_mode;

    void transition_preoperational();
    void transition_operational();
    void write_od_u8(uint16_t index, uint8_t sub, uint8_t value);
    void write_od_u16(uint16_t index, uint8_t sub, uint16_t value);
    void write_od_u32(uint16_t index, uint8_t sub, uint32_t value);
    void write_od_i32(uint16_t index, uint8_t sub, int32_t value);
    void sdo_read(uint16_t index, uint8_t sub);
    void write_rpdo_mapping(uint32_t *entries, uint8_t entry_count, uint8_t rpdo);
    void configure_rpdos();
    void configure_constants();
    void handle_heartbeat(const uint8_t *const data);
    void handle_sdo_reply(const uint8_t *const data);
    void handle_tpdo1(const uint8_t *const data);
    void handle_tpdo2(const uint8_t *const data);
    void process_status_word_generic(const uint16_t status_word);
    void process_status_word_pp(const uint16_t status_word);
    void process_status_word_pv(const uint16_t status_word);
    void send_control_word(uint16_t value);
    void send_target_position(int32_t value);
    void send_target_velocity(int32_t value);

    uint16_t build_ctrl_word(bool new_set_point);

    void wait_for_sdo_writes(uint32_t timeout_ms);
    void enter_position_mode(int velocity);
    void enter_velocity_mode(int velocity);

    void set_profile_acceleration(uint16_t acceleration);
    void set_profile_deceleration(uint16_t deceleration);
    void set_profile_quick_stop_deceleration(uint16_t deceleration);

protected:
    void set_error_descriptions() override;

public:
    CanOpenMotor(const std::string &name, const Can_ptr can, int64_t node_id);
    void subscribe_to_can();
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

    void stop() override;
    double get_position() override;
    void position(const double position, const double speed, const double acceleration) override;
    double get_speed() override;
    void speed(const double speed, const double acceleration) override;
};
