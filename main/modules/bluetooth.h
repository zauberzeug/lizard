#pragma once

#include "module.h"
#include <esp32-ble-command.h>
#include <string>

class Bluetooth : public Module {
private:
    const std::string device_name;

public:
    Bluetooth(const std::string name, const std::string device_name);
    void init(void (*message_handler)(const char *));
};