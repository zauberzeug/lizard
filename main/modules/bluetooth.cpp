#include "bluetooth.h"
#include "../storage.h"
#include "uart.h"

REGISTER_MODULE_DEFAULTS(Bluetooth)

const std::map<std::string, Variable_ptr> Bluetooth::get_defaults() {
    return {};
}

Bluetooth::Bluetooth(const std::string name, const std::string device_name, MessageHandler message_handler)
    : Module(bluetooth, name), device_name(device_name) {
    ZZ::BleCommand::init(device_name, [message_handler](const std::string_view &message) {
        try {
            std::string message_string(message.data(), message.length());
            message_handler(message_string.c_str(), true, false);
        } catch (const std::exception &e) {
            echo("error in bluetooth message handler: %s", e.what());
        }
    });
    this->properties = Bluetooth::get_defaults();
}

void Bluetooth::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "send") {
        expect(arguments, 1, string);
        ZZ::BleCommand::send(arguments[0]->evaluate_string());
    } else if (method_name == "set_pin") {
        expect(arguments, 1, string);
        std::string pin = arguments[0]->evaluate_string();

        // Efficient PIN validation for ESP32
        if (pin.length() != 6) {
            throw std::runtime_error("PIN must be exactly 6 digits");
        }
        for (char c : pin) {
            if (c < '0' || c > '9') {
                throw std::runtime_error("PIN contains invalid characters");
            }
        }

        Storage::set_user_pin(pin);
        echo("User PIN set successfully");
    } else if (method_name == "get_pin") {
        expect(arguments, 0);
        std::string pin = Storage::get_user_pin();
        if (pin.empty()) {
            echo("No user PIN set");
        } else {
            echo(pin.c_str());
        }
    } else if (method_name == "remove_pin") {
        expect(arguments, 0);
        Storage::remove_user_pin();
        echo("User PIN removed");
    } else {
        Module::call(method_name, arguments);
    }
}
