#pragma once

#include "module.h"
#include "serial.h"
#include <string>

class Expander;
using Expander_ptr = std::shared_ptr<Expander>;

class Expander : public Module {
private:
    enum BootState {
        BOOT_INIT,
        BOOT_WAITING,
        BOOT_RESTARTING,
        BOOT_SETTING_UP_PROXIES,
        BOOT_READY
    };

    struct PendingProxy {
        std::string module_name;
        std::string module_type;
        std::vector<ConstExpression_ptr> arguments;
        bool is_setup = false;
    };

    unsigned long int last_message_millis = 0;
    bool heartbeat_request_pending = false;
    BootState boot_state;
    unsigned long boot_start_time;
    std::vector<PendingProxy> pending_proxies;

    static constexpr unsigned long HEARTBEAT_TIMEOUT_MS = 10000;         // 10 seconds
    static constexpr unsigned long HEARTBEAT_RESPONSE_TIMEOUT_MS = 5000; // 5 seconds

    void handle_boot_process();
    void setup_proxy(const PendingProxy &proxy);
    void handle_heartbeat();
    void prepare_restart();

public:
    const ConstSerial_ptr serial;
    const gpio_num_t boot_pin;
    const gpio_num_t enable_pin;
    const u_int16_t boot_wait_time;
    MessageHandler message_handler;

    Expander(const std::string name,
             const ConstSerial_ptr serial,
             const gpio_num_t boot_pin,
             const gpio_num_t enable_pin,
             const u_int16_t boot_wait_time,
             MessageHandler message_handler);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void add_proxy(const std::string module_name,
                   const std::string module_type,
                   const std::vector<ConstExpression_ptr> arguments);
};
