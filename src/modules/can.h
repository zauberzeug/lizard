#pragma once

#include <map>
#include "driver/gpio.h"
#include "module.h"

class Can : public Module
{
private:
    std::map<uint32_t, Module *> subscribers;

public:
    Can(std::string name, gpio_num_t rx_pin, gpio_num_t tx_pin, long baud_rate);
    void step();
};