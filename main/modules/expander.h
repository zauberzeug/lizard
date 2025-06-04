#pragma once

#include "expandable.h"
#include "module.h"
#include "serial.h"
#include <string>

class Expander;
using Expander_ptr = std::shared_ptr<Expander>;

class Expander : public Module, public Expandable {
private:
    unsigned long int last_message_millis = 0;
    bool ping_pending = false;
    unsigned long boot_start_time;

    void deinstall();
    void check_boot_progress();
    void ping();
    void restart();
    void handle_messages(bool check_for_strapping_pins = false);
    void check_strapping_pins(const char *buffer);

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
    void send_proxy(const std::string module_name, const std::string module_type, const std::vector<ConstExpression_ptr> arguments) override;
    void send_property(const std::string proxy_name, const std::string property_name, const ConstExpression_ptr expression) override;
    void send_call(const std::string proxy_name, const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
    bool is_ready() const override;
};
