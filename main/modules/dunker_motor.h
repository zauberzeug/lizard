#pragma once

#include "can.h"
#include "module.h"
#include <cstdint>
#include <memory>

class DunkerMotor;
using DunkerMotor_ptr = std::shared_ptr<DunkerMotor>;

class DunkerMotor : public Module, public std::enable_shared_from_this<DunkerMotor> {
    Can_ptr can;
    const uint8_t node_id;

public:
    DunkerMotor(const std::string &name, const Can_ptr can, int64_t node_id);
    void subscribe_to_can();
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) override;
};
