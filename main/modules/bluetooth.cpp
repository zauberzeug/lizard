#include "bluetooth.h"
#include "../storage.h"
#include "../utils/uart.h"
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
        expect(arguments, 1, integer);
        const int64_t pin = arguments[0]->evaluate_integer();
        if (pin < 0 || pin > 999999) {
            throw std::runtime_error("PIN must be a 6-digit non-negative integer (000000-999999)");
        }
        Storage::set_user_pin(static_cast<std::uint32_t>(pin));
        echo("User PIN set successfully");
    } else if (method_name == "get_pin") {
        expect(arguments, 0);
        std::uint32_t pin;
        if (Storage::get_user_pin(pin)) {
            echo("%06u", static_cast<unsigned>(pin));
        } else {
            echo("No user PIN set");
        }
    } else if (method_name == "remove_pin") {
        expect(arguments, 0);
        Storage::remove_user_pin();
        echo("User PIN removed");
    } else if (method_name == "reset_bonds") {
        expect(arguments, 0);
        ZZ::BleCommand::reset_bonds();
        echo("Bluetooth bonds reset. Restart ESP to put the changes into effect.");
    } else {
        Module::call(method_name, arguments);
    }
}
