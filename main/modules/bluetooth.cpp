#include "bluetooth.h"
#include "uart.h"

Bluetooth::Bluetooth(const std::string name, const std::string device_name, MessageHandler message_handler)
    : Module(bluetooth, name), device_name(device_name) {
    ZZ::BleCommand::init(device_name, [message_handler](const std::string_view &message) {
        try {
            std::string message_string(message.data(), message.length());
            echo("received bluetooth message: %s", message_string.c_str());
            message_handler(message_string.c_str(), true, false);
        } catch (const std::exception &e) {
            echo("error in bluetooth message handler: %s", e.what());
        }
    });
}

void Bluetooth::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "send") {
        expect(arguments, 1, string);
        ZZ::BleCommand::send(arguments[0]->evaluate_string());
    } else {
        Module::call(method_name, arguments);
    }
}
