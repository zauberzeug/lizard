#pragma once

#include "module.h"
#include <cstdint>
#include <esp32-ble-command.h>
#include <string>

class Bluetooth;
using Bluetooth_ptr = std::shared_ptr<Bluetooth>;
using ConstBluetooth_ptr = std::shared_ptr<const Bluetooth>;

class Bluetooth : public Module {
private:
    const std::string device_name;
    std::uint32_t passkey;
    bool security_enabled;

public:
    Bluetooth(const std::string name, const std::string device_name, MessageHandler message_handler);
    Bluetooth(const std::string name, const std::string device_name, MessageHandler message_handler,
              std::uint32_t passkey);

    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
