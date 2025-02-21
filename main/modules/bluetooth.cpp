#include "bluetooth.h"
#include "uart.h"

REGISTER_MODULE_DEFAULTS(Bluetooth)

const std::map<std::string, Variable_ptr> Bluetooth::get_defaults() {
    return {
        {"error_code", std::make_shared<IntegerVariable>(0)},
    };
}

void Bluetooth::set_error_descriptions() {
    error_descriptions = {
        {0x01, "Setup failed"},
        {0x02, "Send failed"},
    };
}

Bluetooth::Bluetooth(const std::string name, const std::string device_name, MessageHandler message_handler)
    : Module(bluetooth, name), device_name(device_name) {
    this->properties["error_code"] = std::make_shared<IntegerVariable>(0);
    ZZ::BleCommand::init(device_name, [this, message_handler](const std::string_view &message) {
        try {
            std::string message_string(message.data(), message.length());
            message_handler(message_string.c_str(), true, false);
        } catch (const std::exception &e) {
            echo("error in bluetooth message handler: %s", e.what());
            this->set_error(0x01);
        }
    });
}

void Bluetooth::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "send") {
        expect(arguments, 1, string);
        int err = ZZ::BleCommand::send(arguments[0]->evaluate_string());
        if (err != 0) {
            this->set_error(0x02);
        }
    } else {
        Module::call(method_name, arguments);
    }
}
