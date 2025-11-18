#pragma once

#include "expandable.h"
#include "module.h"
#include "serial.h"
#include <map>
#include <string>

class PlexusExpander;
using PlexusExpander_ptr = std::shared_ptr<PlexusExpander>;

class PlexusExpander : public Module, public Expandable {
private:
    static const int MAX_RETRIES = 10;
    static const int RETRY_DELAY_MS = 200;
    static const int STEP_TIMEOUT_MS = 1000;
    static const int BOOT_DELAY_MS = 30;
    static const size_t MSG_BUFFER_SIZE = 1024;

    unsigned long int last_message_time = 0;
    unsigned long boot_start_time;
    char message_buffer[MSG_BUFFER_SIZE];
    size_t buffer_pos = 0;

    void check_boot_progress();
    void restart();
    void process_incoming_messages();
    void queue_command(const char *command);
    void flush_commands();
    std::string format_command(const std::string &command);
    void send_tagged_command(const std::string &command);

public:
    Serial_ptr serial;
    uint8_t device_id;
    MessageHandler message_handler;

    PlexusExpander(const std::string name,
                   ConstSerial_ptr const_serial,
                   const uint8_t device_id,
                   MessageHandler message_handler);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void send_proxy(const std::string module_name, const std::string module_type, const std::vector<ConstExpression_ptr> arguments) override;
    void send_property(const std::string proxy_name, const std::string property_name, const ConstExpression_ptr expression) override;
    void send_call(const std::string proxy_name, const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

    // Expandable interface
    bool is_ready() const override;
    std::string get_name() const override;
};
