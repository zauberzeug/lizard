#include "expander.h"

Expander::Expander(const std::string name, const ConstSerial_ptr serial, const gpio_num_t boot_pin, const gpio_num_t enable_pin)
    : Module(expander, name), serial(serial), boot_pin(boot_pin), enable_pin(enable_pin) {
}