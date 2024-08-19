#include "external_expander.h"

#include "storage.h"
#include "utils/serial-replicator.h"
#include "utils/timing.h"
#include "utils/uart.h"
#include <cstring>

const char XON = 0x11;
const char XOFF = 0x13;
const char FILTER_MODE = 0x80;
const char ID = 0x81;

ExternalExpander::ExternalExpander(const std::string name,
                                   const ConstSerial_ptr serial,
                                   const uint8_t device_id,
                                   MessageHandler message_handler)
    : Module(external_expander, name), serial(serial), device_id(device_id), message_handler(message_handler) {
    serial->enable_line_detection();
    char buffer[1024] = "";
    int len = 0;
    const unsigned long int start = millis();
    do {
        if (millis_since(start) > 1000) {
            echo("warning: external expander is not booting");
            break;
        }
        if (serial->available()) {
            len = serial->read_line(buffer);
            strip(buffer, len);
            echo("%s: %s", name.c_str(), buffer);
        }
    } while (strcmp("Ready.", buffer));
    echo("Ready.");
    // 0x80
    serial->write_checked_line(&FILTER_MODE, 1);
    echo("TEST 1");
}

void ExternalExpander::step() {
    static char buffer[1024];
    // this->serial->write_checked_line_id(this->device_id, &ON, 1);
    while (this->serial->has_buffered_lines()) {
        int len = this->serial->read_line(buffer);
        check(buffer, len);
        if (buffer[0] == '!' && buffer[1] == '!') {
            /* Don't trigger keep-alive from expander updates */
            this->message_handler(&buffer[2], false, true);
        } else {
            echo("%s: %s !", this->name.c_str(), buffer);
        }
    }
    // this->serial->write_checked_line_id(this->device_id, &XOFF, 1);
    Module::step();
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
    } else if (method_name == "xoff") {               // debug
        Module::expect(arguments, 0);
        this->serial->write_checked_line_id(this->device_id, &XOFF, 1);
    } else if (method_name == "xon") { // debug
        Module::expect(arguments, 0);
        this->serial->write_checked_line_id(this->device_id, &XON, 1);
    } else if (method_name == "filter") { // debug
        Module::expect(arguments, 0);
        this->serial->write_checked_line(&FILTER_MODE, 1);
    } else if (method_name == "idmV") { // debug
        Module::expect(arguments, 0);
        // write to core a version comman with the ID 0xFF infront of the message like [0xFF][core.version()]
        static char buffer[1024];
        int pos = std::sprintf(buffer, "\xFF");
        // echo("pos: %d", pos);
        pos += std::sprintf(&buffer[pos], "core.version()");
        pos += std::sprintf(&buffer[pos], "\xFE");
        // echo("pos2: %d", pos);
        this->serial->write_checked_line(buffer, pos);
    } else if (method_name == "idmX") { // debug
        Module::expect(arguments, 0);
        // write to core a version comman with the ID 0xFF infront of the message like [0xFF][core.version()]
        static char buffer[1024];
        int pos = std::sprintf(buffer, "core.version()");
        // echo("pos2: %d", pos);
        this->serial->write_checked_line_id(this->device_id, buffer, pos);
    } else {
        static char buffer[1024];
        int pos = std::sprintf(buffer, "core.%s(", method_name.c_str());
        pos += write_arguments_to_buffer(arguments, &buffer[pos]);
        pos += std::sprintf(&buffer[pos], ")");
        this->serial->write_checked_line_id(this->device_id, buffer, pos);
        echo("%s: %s", this->name.c_str(), buffer); // Echo the generic command
    }
}
