#pragma once

#include "module.h"
#include "serial.h"
#include <map>
#include <string>

class ExternalExpander : public Module {
private:
    const ConstSerial_ptr serial;
    unsigned long int last_message_millis = 0;
    bool ping_pending = false;
    std::map<uint8_t, std::string> expander_names; // Maps expander IDs to their names

    void handle_messages();
    void ping();

public:
    ExternalExpander(const std::string name, const ConstSerial_ptr serial);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;

    // Register a new external expander
    void register_expander(uint8_t id, const std::string &name);

    static const std::map<std::string, Variable_ptr> get_defaults();
};