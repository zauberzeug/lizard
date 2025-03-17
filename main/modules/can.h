#pragma once

#include "driver/gpio.h"
#include "module.h"
#include <memory>

class Can;
using Can_ptr = std::shared_ptr<Can>;

class Can : public Module {
private:
    std::map<uint32_t, Module_ptr> subscribers;

public:
    Can(const std::string name, const gpio_num_t rx_pin, const gpio_num_t tx_pin, const long baud_rate);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

    bool receive();
    void send(const uint32_t id, const uint8_t data[8], const bool rtr = false, const uint8_t dlc = 8, const bool extended = false) const;
    void send(const uint32_t id,
              const uint8_t d0, const uint8_t d1, const uint8_t d2, const uint8_t d3,
              const uint8_t d4, const uint8_t d5, const uint8_t d6, const uint8_t d7,
              const bool rtr = false) const;
    void subscribe(const uint32_t id, const Module_ptr module);
};
