#pragma once

#include "can.h"
#include "module.h"
#include <memory>

class RmdMotor;
using RmdMotor_ptr = std::shared_ptr<RmdMotor>;
using ConstRmdMotor_ptr = std::shared_ptr<const RmdMotor>;

class RmdMotor : public Module, public std::enable_shared_from_this<RmdMotor> {
private:
    const uint32_t motor_id;
    const Can_ptr can;
    uint8_t last_msg_id = 0;
    int ratio;
    const double encoder_range;
    int32_t last_encoder_position;
    bool has_last_encoder_position = false;
    unsigned long int last_msg_millis = 0;

    bool send(const uint8_t d0, const uint8_t d1, const uint8_t d2, const uint8_t d3,
              const uint8_t d4, const uint8_t d5, const uint8_t d6, const uint8_t d7,
              const unsigned long int timeout_ms = 3);

public:
    RmdMotor(const std::string name, const Can_ptr can, const uint8_t motor_id, const int ratio);
    void subscribe_to_can();
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) override;
    static const std::map<std::string, Variable_ptr> &get_defaults();

    bool power(double target_power);
    bool speed(double target_speed);
    bool position(double target_position, double target_speed = 0.0);
    bool stop();
    bool off();
    bool hold();
    bool clear_errors();

    double get_position() const;
    double get_speed() const;
    bool set_acceleration(const uint8_t index, const uint32_t acceleration);
};
