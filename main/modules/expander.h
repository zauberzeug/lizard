#pragma once

#include "module.h"
#include "serial.h"
#include <string>

class Expander;
using Expander_ptr = std::shared_ptr<Expander>;

struct proxy_frame_field_t {
    Variable_ptr variable;
    char type; // ? i f
};

class Expander : public Module {
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

    // with "frames" on, the expander sends the properties of all proxies as one frame per step
    std::vector<proxy_frame_field_t> frame_fields;
    std::vector<std::string> frame_proxies;
    size_t frame_numeric_length = 0;
    size_t frame_bit_count = 0;
    unsigned long frame_defined_millis = 0;
    bool frame_seen = false;
    bool frame_seq_valid = false;
    uint8_t frame_seq = 0;
    unsigned long frame_warning_millis = 0;
    int64_t reported_frame_errors = 0;
    int64_t reported_frame_gaps = 0;
    void handle_frame(const char *line, size_t length);
    void check_frames();

public:
    static inline constexpr const char *TYPE = "Expander";

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
    void send_proxy(const std::string module_name,
                    const std::string module_type,
                    const std::vector<ConstExpression_ptr> arguments,
                    const std::map<std::string, Variable_ptr> &properties);
    void send_property(const std::string proxy_name, const std::string property_name, const ConstExpression_ptr expression);
    void send_call(const std::string proxy_name, const std::string method_name, const std::vector<ConstExpression_ptr> arguments);
    static const std::map<std::string, Variable_ptr> get_defaults();
};
