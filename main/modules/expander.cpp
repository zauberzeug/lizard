#include "expander.h"

#include "utils/echo.h"

Expander::Expander(const std::string name, const ConstSerial_ptr serial, const gpio_num_t boot_pin, const gpio_num_t enable_pin)
    : Module(expander, name), serial(serial), boot_pin(boot_pin), enable_pin(enable_pin) {
    serial->enable_line_detection();
}

void Expander::step() {
    static char buffer[1024];
    if (this->serial->available()) {
        int len = this->serial->read_line(buffer);
        buffer[len] = 0;
        echo(buffer);
    }
    Module::step();
}
