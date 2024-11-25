#pragma once

#include "can.h"
#include "module.h"
#include <cstdint>
#include <memory>

class DunkerMotor;
using DunkerMotor_ptr = std::shared_ptr<DunkerMotor>;

class DunkerMotor : public Module, public std::enable_shared_from_this<DunkerMotor> {
private:
    Can_ptr can;
    const uint8_t node_id;
    int waiting_nmt_writes = 0;
    int waiting_sdo_writes = 0;

    void sdo_read(const uint16_t index, const uint8_t sub);
    void nmt_write(const uint8_t cs);
    void sdo_write(const uint16_t index, const uint8_t sub, const uint8_t bits, const uint32_t value, const bool wait = true);
    void wait();

public:
    DunkerMotor(const std::string &name, const Can_ptr can, int64_t node_id);
    void subscribe_to_can();
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) override;
    static const std::map<std::string, Variable_ptr> &get_defaults();
    void speed(const double speed);
    double get_speed();
};
