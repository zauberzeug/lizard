#pragma once

#include "expandable.h"
#include "module.h"
#include "serial.h"
#include <map>
#include <string>
#include <vector>

class PlexusExpander;
using PlexusExpander_ptr = std::shared_ptr<PlexusExpander>;

class PlexusExpander : public Module, public Expandable {
private:
    unsigned long int last_message_millis = 0;
    bool ping_pending = false;
    unsigned long boot_start_time;

    static const size_t MSG_BUFFER_SIZE = 1024;
    char message_buffer[MSG_BUFFER_SIZE];
    size_t buffer_pos = 0;

    void check_boot_progress();
    void restart();
    void handle_messages();
    void buffer_message(const char *message);

public:
    Serial_ptr serial;
    char expander_id;
    MessageHandler message_handler;

    PlexusExpander(const std::string name,
                   ConstSerial_ptr const_serial,
                   const char expander_id,
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