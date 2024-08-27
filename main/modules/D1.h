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

    void sdo_read(const uint16_t index, const uint8_t sub);
    void nmt_write(const uint8_t cs);
    void sdo_write(const uint16_t index, const uint8_t sub, const uint8_t bits, const uint32_t value, const bool wait = true);
    void set_mode(const uint8_t mode);
    void wait();

public:
    D1Motor(const std::string &name, const Can_ptr can, int64_t node_id);
    void subscribe_to_can();
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) override;
    void homing();
    void ppMode(const int position);
    void speedMode(const int speed);
    void reset();
    void stop();
    void setup();
    void step() override;
};