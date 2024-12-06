#include "expander.h"

#include "storage.h"
#include "utils/serial-replicator.h"
#include "utils/string_utils.h"
#include "utils/timing.h"
#include "utils/uart.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

const std::map<std::string, Variable_ptr> Expander::get_defaults() {
    return {
        {"boot_timeout", std::make_shared<NumberVariable>(5.0)},
        {"ping_interval", std::make_shared<NumberVariable>(1.0)},
        {"ping_timeout", std::make_shared<NumberVariable>(2.0)},
        {"is_ready", std::make_shared<BooleanVariable>(false)},
        {"last_message_age", std::make_shared<IntegerVariable>(0)},
    };
}

Expander::Expander(const std::string name,
                   const ConstSerial_ptr serial,
                   const gpio_num_t boot_pin,
                   const gpio_num_t enable_pin,
                   MessageHandler message_handler)
    : Module(expander, name),
      serial(serial),
      boot_pin(boot_pin),
      enable_pin(enable_pin),
      message_handler(message_handler) {

    this->merge_properties(Expander::get_defaults());

    this->serial->enable_line_detection();
    if (boot_pin != GPIO_NUM_NC && enable_pin != GPIO_NUM_NC) {
        gpio_reset_pin(boot_pin);
        gpio_reset_pin(enable_pin);
        gpio_set_direction(boot_pin, GPIO_MODE_OUTPUT);
        gpio_set_direction(enable_pin, GPIO_MODE_OUTPUT);
        gpio_set_level(boot_pin, 1);
    }

    this->restart();
}

void Expander::step() {
    if (!this->properties.at("is_ready")->boolean_value) {
        this->check_boot_progress();
    } else {
        this->ping();
        this->handle_messages();
    }
    this->properties.at("last_message_age")->integer_value = millis_since(this->last_message_millis);
    Module::step();
}

void Expander::check_boot_progress() {
    static char buffer[1024];
    while (this->serial->has_buffered_lines()) {
        const int len = this->serial->read_line(buffer, sizeof(buffer));
        check(buffer, len);
        this->last_message_millis = millis();
        echo("%s: %s", this->name.c_str(), buffer);
        if (strcmp("Ready.", buffer) == 0) {
            for (auto &proxy : this->proxies) {
                if (!proxy.is_setup) {
                    this->setup_proxy(proxy);
                }
            }
            this->properties.at("is_ready")->boolean_value = true;
            echo("%s: Booting process completed successfully", this->name.c_str());
            break;
        }
    }

    const unsigned long boot_timeout = this->get_property("boot_timeout")->number_value * 1000;
    if (boot_timeout > 0 && millis_since(this->boot_start_time) > boot_timeout) {
        echo("warning: expander %s did not send 'Ready.', trying restart", this->name.c_str());
        this->restart();
    }
}

void Expander::ping() {
    const double last_message_age = this->get_property("last_message_age")->integer_value / 1000.0;
    const double ping_interval = this->get_property("ping_interval")->number_value;
    const double ping_timeout = this->get_property("ping_timeout")->number_value;
    if (!this->ping_pending) {
        if (last_message_age >= ping_interval) {
            this->serial->write_checked_line("core.print('__PONG__')");
            this->ping_pending = true;
        }
    } else {
        if (last_message_age >= ping_interval + ping_timeout) {
            echo("warning: expander %s ping timed out, restarting", this->name.c_str());
            this->restart();
        }
    }
}

void Expander::restart() {
    this->ping_pending = false;

    for (auto &proxy : this->proxies) {
        proxy.is_setup = false;
    }

    if (this->boot_pin != GPIO_NUM_NC && this->enable_pin != GPIO_NUM_NC) {
        gpio_set_level(this->enable_pin, 0);
        delay(100);
        gpio_set_level(this->enable_pin, 1);
    } else {
        this->serial->write_checked_line("core.restart()");
    }
    this->boot_start_time = millis();
    this->properties.at("is_ready")->boolean_value = false;
}

void Expander::handle_messages(bool check_for_strapping_pins) {
    static char buffer[1024];
    while (this->serial->has_buffered_lines()) {
        int len = this->serial->read_line(buffer, sizeof(buffer));
        check(buffer, len);
        if (check_for_strapping_pins) {
            this->check_strapping_pins(buffer);
        }
        this->last_message_millis = millis();
        this->ping_pending = false;
        if (buffer[0] == '!' && buffer[1] == '!') {
            this->message_handler(&buffer[2], false, true);
        } else if (strcmp("\"__PONG__\"", buffer) == 0) {
            // No echo for pong
        } else {
            echo("%s: %s", this->name.c_str(), buffer);
        }
    }
}

void Expander::add_proxy(const std::string module_name,
                         const std::string module_type,
                         const std::vector<ConstExpression_ptr> arguments) {
    ProxyInformation proxy = {module_name, module_type, arguments, {}, false};
    this->proxies.push_back(proxy);

    if (this->properties.at("is_ready")->boolean_value) {
        // Reset ready state, since we're not ready until all proxies are setup
        echo("%s: New proxy added, setting up...", this->name.c_str());
        this->setup_proxy(proxy);
    }
}

void Expander::setup_proxy(ProxyInformation &proxy) {
    static char buffer[256];
    int pos = csprintf(buffer, sizeof(buffer), "%s = %s(",
                       proxy.module_name.c_str(), proxy.module_type.c_str());
    pos += write_arguments_to_buffer(proxy.arguments, &buffer[pos], sizeof(buffer) - pos);
    pos += csprintf(&buffer[pos], sizeof(buffer) - pos, "); ");
    pos += csprintf(&buffer[pos], sizeof(buffer) - pos, "%s.broadcast()", proxy.module_name.c_str());
    this->serial->write_checked_line(buffer, pos);
    delay(50);
    for (const auto &[property_name, expression] : proxy.properties) {
        this->setup_property(proxy.module_name, property_name, expression);
    }
    proxy.is_setup = true;
}

void Expander::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "run") {
        Module::expect(arguments, 1, string);
        std::string command = arguments[0]->evaluate_string();
        this->serial->write_checked_line(command.c_str(), command.length());
    } else if (method_name == "restart") {
        Module::expect(arguments, 0);
        restart();
    } else if (method_name == "disconnect") {
        Module::expect(arguments, 0);
        deinstall();
    } else if (method_name == "flash") {
        if (arguments.size() > 1) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, boolean);
        bool force = arguments.size() > 0 ? arguments[0]->evaluate_boolean() : false;
        if (this->boot_pin == GPIO_NUM_NC || this->enable_pin == GPIO_NUM_NC) {
            throw std::runtime_error("expander \"" + this->name + "\" does not support flashing, pins not set");
        }
        Storage::clear_nvs();
        gpio_set_level(this->boot_pin, 0);
        if (!force) {
            this->serial->write_checked_line("core.get_pin_status(0)");
            this->serial->write_checked_line("core.get_pin_status(2)");
            this->serial->write_checked_line("core.get_pin_status(12)");
            delay(100);
            this->handle_messages(true);
        }
        deinstall();
        bool success = ZZ::Replicator::flashReplica(this->serial->uart_num,
                                                    this->enable_pin,
                                                    this->boot_pin,
                                                    this->serial->rx_pin,
                                                    this->serial->tx_pin,
                                                    this->serial->baud_rate);
        Storage::save_startup();
        delay(100);
        this->serial->reinitialize_after_flash();
        if (!success) {
            throw std::runtime_error("could not flash expander \"" + this->name + "\"");
        } else {
            restart();
        }
    } else {
        static char buffer[1024];
        int pos = csprintf(buffer, sizeof(buffer), "core.%s(", method_name.c_str());
        pos += write_arguments_to_buffer(arguments, &buffer[pos], sizeof(buffer) - pos);
        pos += csprintf(&buffer[pos], sizeof(buffer) - pos, ")");
        this->serial->write_checked_line(buffer, pos);
    }
}

void Expander::check_strapping_pins(const char *buffer) {
    // We only need to check GPIO 0, 2 and 12 because they directly influence boot mode and flash voltage selection,
    // ensuring correct operation during boot and flashing. These are not directly controllable by the expander.
    if (strstr(buffer, "GPIO_Status[12]|") != nullptr) {
        if (strstr(buffer, "GPIO_Status[12]| Level: 1") != nullptr) {
            echo("warning: GPIO12 state is HIGH, this can cause issues with flash voltage selection");
        }
    }
    if (strstr(buffer, "GPIO_Status[0]|") != nullptr) {
        if (strstr(buffer, "GPIO_Status[0]| Level: 1") != nullptr) {
            throw std::runtime_error("GPIO0 current state is HIGH - must be LOW for boot mode");
        }
    }
    if (strstr(buffer, "GPIO_Status[2]|") != nullptr) {
        if (strstr(buffer, "GPIO_Status[2]| Level: 1") != nullptr) {
            throw std::runtime_error("GPIO2 current state is HIGH - must be LOW or floating for flash mode");
        }
    }
}

void Expander::deinstall() {
    this->serial->deinstall();
    if (this->boot_pin != GPIO_NUM_NC && this->enable_pin != GPIO_NUM_NC) {
        gpio_reset_pin(this->boot_pin);
        gpio_reset_pin(this->enable_pin);
        gpio_set_direction(this->boot_pin, GPIO_MODE_INPUT);
        gpio_set_direction(this->enable_pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(this->boot_pin, GPIO_FLOATING);
        gpio_set_pull_mode(this->enable_pin, GPIO_FLOATING);
    }
}

void Expander::setup_property(const std::string proxy_name, const std::string property_name, const ConstExpression_ptr expression) {
    static char buffer[256];
    int pos = csprintf(buffer, sizeof(buffer), "%s.%s = ", proxy_name.c_str(), property_name.c_str());
    pos += expression->print_to_buffer(&buffer[pos], sizeof(buffer) - pos);
    this->serial->write_checked_line(buffer, pos);
}

void Expander::add_property(const std::string proxy_name, const std::string property_name, const ConstExpression_ptr expression) {
    for (auto &proxy : proxies) {
        if (proxy.module_name == proxy_name) {
            proxy.properties[property_name] = expression;
            if (proxy.is_setup) {
                this->setup_property(proxy_name, property_name, expression);
            }
            return;
        }
    }
    throw std::runtime_error("Proxy not found: " + proxy_name);
}
