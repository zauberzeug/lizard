#pragma once

#include "module.h"
#include "serial.h"
#include <string>

class Expander;
using Expander_ptr = std::shared_ptr<Expander>;

class Expander : public Module {
private:
    unsigned long int last_message_millis = 0;

    enum BootState {
        BOOT_INIT,
        BOOT_WAITING,
        BOOT_RESTARTING,
        BOOT_READY
    };
    BootState boot_state;
    unsigned long boot_start_time;

    void handle_boot_process();

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
};
