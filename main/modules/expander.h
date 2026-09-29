#pragma once

#include "../utils/telemetry.h"
#include "module.h"
#include "serial.h"
#include <string>
#include <vector>

class Expander;
using Expander_ptr = std::shared_ptr<Expander>;

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

    // telemetry: with telemetry_interval >= 0 the proxies' properties come as frames instead of text broadcasts
    std::vector<std::pair<std::string, std::string>> telemetry_proxies; // name and module type
    bool telemetry_order_pending = false;
    bool telemetry_layout_seen = false;
    unsigned long telemetry_order_millis = 0;
    telemetry::Layout telemetry_layout;
    std::map<uint8_t, std::vector<Variable *>> telemetry_mapped; // per frame: the proxy variable of each field index
    std::map<uint8_t, std::vector<telemetry::Slot>> telemetry_slots;
    Variable_ptr telemetry_frames, telemetry_errors, telemetry_gaps, telemetry_mismatch;
    std::map<uint8_t, uint8_t> telemetry_last_seq;
    void send_telemetry_orders();
    void check_telemetry();
    void handle_telemetry_line(const char *line, int length);
    static void count(const Variable_ptr &counter, int64_t increment = 1);

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
    void send_proxy(const std::string module_name, const std::string module_type, const std::vector<ConstExpression_ptr> arguments);
    void send_property(const std::string proxy_name, const std::string property_name, const ConstExpression_ptr expression);
    void send_call(const std::string proxy_name, const std::string method_name, const std::vector<ConstExpression_ptr> arguments);
    static const std::map<std::string, Variable_ptr> get_defaults();
};
