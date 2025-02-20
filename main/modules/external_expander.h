#pragma once

#include "expandable.h"
#include "module.h"
#include "serial.h"
#include <map>
#include <string>

class ExternalExpander;
using ExternalExpander_ptr = std::shared_ptr<ExternalExpander>;

class ExternalExpander : public Module, public Expandable {
private:
    unsigned long int last_message_millis = 0;
    bool ping_pending = false;
    unsigned long boot_start_time;

    void check_boot_progress();
    void ping();
    void restart();
    void handle_messages();

public:
    const ConstSerial_ptr serial;
    const uint8_t expander_id;
    MessageHandler message_handler;

    ExternalExpander(const std::string name,
                     const ConstSerial_ptr serial,
                     uint8_t expander_id,
                     MessageHandler message_handler);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void send_proxy(const std::string module_name, const std::string module_type, const std::vector<ConstExpression_ptr> arguments) override;
    void send_property(const std::string proxy_name, const std::string property_name, const ConstExpression_ptr expression) override;
    void send_call(const std::string proxy_name, const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

    // Expandable interface
    bool is_ready() const override;
};