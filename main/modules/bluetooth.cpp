#include "bluetooth.h"
#include "uart.h"

REGISTER_MODULE_DEFAULTS(Bluetooth)

const std::map<std::string, Variable_ptr> Bluetooth::get_defaults() {
    return {
        {"passkey", std::make_shared<IntegerVariable>(123456)},
        {"security_enabled", std::make_shared<BooleanVariable>(false)},
    };
}

Bluetooth::Bluetooth(const std::string name, const std::string device_name, MessageHandler message_handler)
    : Module(bluetooth, name), device_name(device_name), passkey(123456), security_enabled(false) {
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

Bluetooth::Bluetooth(const std::string name, const std::string device_name, MessageHandler message_handler,
                     std::uint32_t passkey)
    : Module(bluetooth, name), device_name(device_name), passkey(passkey), security_enabled(true) {
    ZZ::BleCommand::init(device_name, [message_handler](const std::string_view &message) {
            try {
                std::string message_string(message.data(), message.length());
                message_handler(message_string.c_str(), true, false);
            } catch (const std::exception &e) {
                echo("error in bluetooth message handler: %s", e.what());
            } }, passkey, [](std::uint32_t displayed_passkey) { echo("BLE Pairing: Enter passkey %06d on the connecting device", displayed_passkey); }, [](bool success, std::uint16_t conn_handle) {
            if (success) {
                echo("BLE authentication successful for connection %d", conn_handle);
            } else {
                echo("BLE authentication failed for connection %d", conn_handle);
            } });

    auto defaults = Bluetooth::get_defaults();
    defaults["passkey"]->integer_value = passkey;
    defaults["security_enabled"]->boolean_value = true;
    this->properties = defaults;
}

void Bluetooth::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "send") {
        expect(arguments, 1, string);
        ZZ::BleCommand::send(arguments[0]->evaluate_string());
    } else {
        Module::call(method_name, arguments);
    }
}
