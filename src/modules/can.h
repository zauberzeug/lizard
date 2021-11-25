#pragma once

#include "driver/gpio.h"
#include "module.h"

class Can : public Module
{
private:
    std::map<uint32_t, Module *> subscribers;

public:
    Can(std::string name, gpio_num_t rx_pin, gpio_num_t tx_pin, long baud_rate);
    void step();
    void send(uint32_t id, uint8_t data[8], bool rtr = false);
    void send(uint32_t id,
              uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
              uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7,
              bool rtr = false);
    void call(std::string method_name, std::vector<Expression *> arguments);
    void subscribe(uint32_t id, Module *module);
};