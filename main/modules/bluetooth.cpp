#include "bluetooth.h"
#include "echo.h"

Bluetooth::Bluetooth(const std::string name, const std::string device_name)
    : Module(bluetooth, name), device_name(device_name) {
}

void Bluetooth::init(void (*message_handler)(const char *)) {
    ZZ::BleCommand::init(this->device_name, [message_handler](const std::string_view &message) {
        try {
            std::string message_string(message.data(), message.length());
            message_handler(message_string.c_str());
        } catch (const std::exception &e) {
            echo("error in bluetooth message handler: %s", e.what());
        }
    });
}
