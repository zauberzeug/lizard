#pragma once

#include "module.h"
#include "serial.h"
#include <string>

class Expander;
using Expander_ptr = std::shared_ptr<Expander>;

class Expander : public Module {
private:
    struct ProxyInformation {
        std::string module_name;
        std::string module_type;
        std::vector<ConstExpression_ptr> arguments;
        bool is_setup = false;
    };

    unsigned long int last_message_millis = 0;
    bool ping_pending = false;
    unsigned long boot_start_time;
    std::vector<ProxyInformation> proxies;

    void check_boot_progress();
    void ping();
    void restart();
    void handle_messages();
    void setup_proxy(ProxyInformation &proxy);
    void check_strapping_pins();
    void handle_pin_status(const char *buffer);

public:
    const ConstSerial_ptr serial;
    const gpio_num_t boot_pin;
    const gpio_num_t enable_pin;
    MessageHandler message_handler;

    Expander(const std::string name,
             const ConstSerial_ptr serial,
             const gpio_num_t boot_pin,
             const gpio_num_t enable_pin,
             MessageHandler message_handler);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void add_proxy(const std::string module_name,
                   const std::string module_type,
                   const std::vector<ConstExpression_ptr> arguments);
};
