#include "expander.h"

#include "storage.h"
#include "utils/serial-replicator.h"
#include "utils/timing.h"
#include "utils/uart.h"
#include <cstring>

Expander::Expander(const std::string name,
                   const ConstSerial_ptr serial,
                   const gpio_num_t boot_pin,
                   const gpio_num_t enable_pin,
                   void (*message_handler)(const char *))
    : Module(expander, name), serial(serial), boot_pin(boot_pin), enable_pin(enable_pin), message_handler(message_handler) {
    serial->enable_line_detection();
    gpio_reset_pin(boot_pin);
    gpio_reset_pin(enable_pin);
    gpio_set_direction(boot_pin, GPIO_MODE_OUTPUT);
    gpio_set_direction(enable_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(boot_pin, 1);
    gpio_set_level(enable_pin, 0);
    delay(100);
    gpio_set_level(enable_pin, 1);
    char buffer[1024] = "";
    int len = 0;
    const unsigned long int start = millis();
    do {
        if (millis_since(start) > 1000) {
            echo("warning: expander is not booting");
            break;
        }
        if (serial->available()) {
            len = serial->read_line(buffer);
            strip(buffer, len);
            echo("%s: %s", name.c_str(), buffer);
        }
    } while (strcmp("Ready.", buffer));
}

void Expander::step() {
    static char buffer[1024];
    while (this->serial->has_buffered_lines()) {
        int len = this->serial->read_line(buffer);
        check(buffer, len);
        if (buffer[0] == '!' && buffer[1] == '!') {
            this->message_handler(&buffer[2]);
        } else {
            echo("%s: %s", this->name.c_str(), buffer);
        }
    }
    Module::step();
}

void Expander::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "run") {
        Module::expect(arguments, 1, string);
        std::string command = arguments[0]->evaluate_string();
        this->serial->write_checked_line(command.c_str(), command.length());
    } else if (method_name == "disconnect") {
        Module::expect(arguments, 0);
        this->serial->deinstall();
        gpio_reset_pin(this->boot_pin);
        gpio_reset_pin(this->enable_pin);
        gpio_set_direction(this->boot_pin, GPIO_MODE_INPUT);
        gpio_set_direction(this->enable_pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(this->boot_pin, GPIO_FLOATING);
        gpio_set_pull_mode(this->enable_pin, GPIO_FLOATING);
    } else if (method_name == "flash") {
        Storage::clear_nvs();
        this->serial->deinstall();
        Module::expect(arguments, 0);
        bool success = ZZ::Replicator::flashReplica(this->serial->uart_num,
                                                    this->enable_pin,
                                                    this->boot_pin,
                                                    this->serial->rx_pin,
                                                    this->serial->tx_pin,
                                                    this->serial->baud_rate);
        Storage::save_startup();
        if (!success) {
            throw std::runtime_error("could not flash expander \"" + this->name + "\"");
        }
    } else {
        static char buffer[1024];
        int pos = std::sprintf(buffer, "core.%s(", method_name.c_str());
        pos += write_arguments_to_buffer(arguments, &buffer[pos]);
        pos += std::sprintf(&buffer[pos], ")");
        this->serial->write_checked_line(buffer, pos);
    }
}
