#pragma once

#include "module.h"
#include "serial.h"
#include <string>

class ExternalExpander;
using ExternalExpander_ptr = std::shared_ptr<ExternalExpander>;

class ExternalExpander : public Module {
public:
    const ConstSerial_ptr serial;
    const uint8_t device_id;
    MessageHandler message_handler;

    ExternalExpander(const std::string name,
                     const ConstSerial_ptr serial,
                     const uint8_t device_id,
                     MessageHandler message_handler);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
};
