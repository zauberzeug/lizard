#pragma once

#include "can.h"
#include "module.h"
#include <cstdint>
#include <memory>

class D1Motor;
using D1Motor_ptr = std::shared_ptr<D1Motor>;

class D1Motor : public Module, public std::enable_shared_from_this<D1Motor> {
private:
    Can_ptr can;
    const uint8_t node_id;
    int waiting_nmt_writes = 0;
    int waiting_sdo_writes = 0;
    bool enabled = true;

    void sdo_read(const uint16_t index, const uint8_t sub);
    void nmt_write(const uint8_t cs);
    void sdo_write(const uint16_t index, const uint8_t sub, const uint8_t bits, const uint32_t value, const bool wait = true);
    void wait();

public:
    D1Motor(const std::string &name, const Can_ptr can, int64_t node_id);
    void subscribe_to_can();
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void step() override;
    void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
    void setup();
    void home();
    void profile_position(const int32_t position);
    void profile_velocity(const int32_t velocity);
    void stop();
    void enable();
    void disable();
};
