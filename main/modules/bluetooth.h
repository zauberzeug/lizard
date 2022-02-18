#pragma once

#include "module.h"
#include <esp32-ble-command.h>
#include <string>

class Bluetooth;
using Bluetooth_ptr = std::shared_ptr<Bluetooth>;
using ConstBluetooth_ptr = std::shared_ptr<const Bluetooth>;

class Bluetooth : public Module {
private:
    const std::string device_name;

public:
    Bluetooth(const std::string name, const std::string device_name, void (*message_handler)(const char *));
};