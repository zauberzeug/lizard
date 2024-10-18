#include "expander.h"

#include "storage.h"
#include "utils/serial-replicator.h"
#include "utils/timing.h"
#include "utils/uart.h"
#include <cstring>
#include <stdexcept>

Expander::Expander(const std::string name,
                   const ConstSerial_ptr serial,
                   const gpio_num_t boot_pin,
                   const gpio_num_t enable_pin,
                   const u_int16_t boot_wait_time,
                   MessageHandler message_handler)
    : Module(expander, name), serial(serial), boot_pin(boot_pin), enable_pin(enable_pin), boot_wait_time(boot_wait_time), message_handler(message_handler) {
    this->properties["last_message_age"] = std::make_shared<IntegerVariable>();
    this->properties["is_ready"] = std::make_shared<BooleanVariable>(false);

    serial->enable_line_detection();
    if (boot_pin != GPIO_NUM_NC && enable_pin != GPIO_NUM_NC) {
        gpio_reset_pin(boot_pin);
        gpio_reset_pin(enable_pin);
        gpio_set_direction(boot_pin, GPIO_MODE_OUTPUT);
        gpio_set_direction(enable_pin, GPIO_MODE_OUTPUT);
        gpio_set_level(boot_pin, 1);
        gpio_set_level(enable_pin, 0);
        delay(100);
        gpio_set_level(enable_pin, 1);
    } else {
        serial->write_checked_line("core.restart()", 14);
    }

    const unsigned long int start = millis();

    if (!wait_for_ready_message(start, this->boot_wait_time)) {
        if (this->boot_wait_time > 0) {
            echo("warning: expander %s did not boot in the expected time (%d ms)", name.c_str(), this->boot_wait_time);
        } else {
            echo("error: expander %s did not boot successfully even after multiple attempts", name.c_str());
        }
    }
}

void Expander::step() {
    static char buffer[1024];
    while (this->serial->has_buffered_lines()) {
        int len = this->serial->read_line(buffer);
        check(buffer, len);
        this->last_message_millis = millis();
        if (buffer[0] == '!' && buffer[1] == '!') {
            this->message_handler(&buffer[2], false, true); // Don't trigger core keep-alive from expander broadcasts
        } else {
            echo("%s: %s", this->name.c_str(), buffer);
        }
    }
    this->properties.at("last_message_age")->integer_value = millis_since(this->last_message_millis);
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
        if (this->boot_pin != GPIO_NUM_NC && this->enable_pin != GPIO_NUM_NC) {
            gpio_reset_pin(this->boot_pin);
            gpio_reset_pin(this->enable_pin);
            gpio_set_direction(this->boot_pin, GPIO_MODE_INPUT);
            gpio_set_direction(this->enable_pin, GPIO_MODE_INPUT);
            gpio_set_pull_mode(this->boot_pin, GPIO_FLOATING);
            gpio_set_pull_mode(this->enable_pin, GPIO_FLOATING);
        }
    } else if (method_name == "flash") {
        Module::expect(arguments, 0);
        if (this->boot_pin == GPIO_NUM_NC || this->enable_pin == GPIO_NUM_NC) {
            throw std::runtime_error("expander \"" + this->name + "\" does not support flashing, pins not set");
        }
        Storage::clear_nvs();
        this->serial->deinstall();
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

bool Expander::wait_for_ready_message(unsigned long start, unsigned long max_wait_time) {
    char buffer[1024] = "";
    int len = 0;

    while (max_wait_time == 0 || millis_since(start) <= max_wait_time) {
        if (serial->available()) {
            len = serial->read_line(buffer);
            strip(buffer, len);
            echo("%s: %s", this->name.c_str(), buffer);

            if (strcmp("Ready.", buffer) == 0) {
                this->properties.at("is_ready")->boolean_value = true;
                echo("%s: Booting process completed successfully", this->name.c_str());
                return true;
            }
        }

        if (max_wait_time == 0 && millis_since(start) >= 30000) {
            echo("Warning: expander %s did not send 'Ready.', trying restart", this->name.c_str());
            if (boot_pin != GPIO_NUM_NC && enable_pin != GPIO_NUM_NC) {
                gpio_reset_pin(boot_pin);
                gpio_reset_pin(enable_pin);
                gpio_set_direction(boot_pin, GPIO_MODE_OUTPUT);
                gpio_set_direction(enable_pin, GPIO_MODE_OUTPUT);
                gpio_set_level(boot_pin, 1);
                gpio_set_level(enable_pin, 0);
                delay(100);
                gpio_set_level(enable_pin, 1);
            } else {
                serial->write_checked_line("core.restart()", 14);
            }
            start = millis();
        }
    }

    return false;
}
