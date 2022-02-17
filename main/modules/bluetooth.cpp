#include "bluetooth.h"
#include "uart.h"

Bluetooth::Bluetooth(const std::string name, const std::string device_name, void (*message_handler)(const char *))
    : Module(bluetooth, name), device_name(device_name) {
    ZZ::BleCommand::init(device_name, [message_handler](const std::string_view &message) {
        try {
            std::string message_string(message.data(), message.length());
            message_handler(message_string.c_str());
        } catch (const std::exception &e) {
            echo("error in bluetooth message handler: %s", e.what());
        }
    });
}
