#include "external_expander.h"

#include "storage.h"
#include "utils/serial-replicator.h"
#include "utils/timing.h"
#include "utils/uart.h"
#include <cstring>

ExternalExpander::ExternalExpander(const std::string name,
                                   const ConstSerial_ptr serial,
                                   const uint8_t device_id,
                                   MessageHandler message_handler)
    : Module(external_expander, name), serial(serial), device_id(device_id), message_handler(message_handler) {
}

void ExternalExpander::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "run") {
        Module::expect(arguments, 1, string);
        std::string command = arguments[0]->evaluate_string();
        this->serial->write_checked_line(command.c_str(), command.length());
        echo("%s: %s", this->name.c_str(), command.c_str()); // Echo the command
    } else if (method_name == "disconnect") {
        Module::expect(arguments, 0);
        this->serial->deinstall();
        echo("%s: disconnected", this->name.c_str()); // Echo the disconnection
    } else {
        static char buffer[1024];
        int pos = std::sprintf(buffer, "core.%s(", method_name.c_str());
        pos += write_arguments_to_buffer(arguments, &buffer[pos]);
        pos += std::sprintf(&buffer[pos], ")");
        this->serial->write_checked_line(buffer, pos);
        echo("%s: %s", this->name.c_str(), buffer); // Echo the generic command
    }
}
