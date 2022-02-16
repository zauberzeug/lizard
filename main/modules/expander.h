#pragma once

#include "module.h"
#include "serial.h"
#include <string>

class Expander;
using Expander_ptr = std::shared_ptr<Expander>;

class Expander : public Module {
private:
    const ConstSerial_ptr serial;
    const gpio_num_t boot_pin;
    const gpio_num_t enable_pin;

public:
    Expander(const std::string name, const ConstSerial_ptr serial, const gpio_num_t boot_pin, const gpio_num_t enable_pin);
};