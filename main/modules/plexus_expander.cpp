#include "plexus_expander.h"
#include "utils/addressing.h"
#include "utils/ota.h"
#include "utils/string_utils.h"
#include "utils/timing.h"
#include "utils/uart.h"
#include <cstdlib>
#include <cstring>
#include <stdexcept>

REGISTER_MODULE_DEFAULTS(PlexusExpander)

const std::map<std::string, Variable_ptr> PlexusExpander::get_defaults() {
    return {
        {"boot_timeout", std::make_shared<NumberVariable>(5.0)},
        {"ping_interval", std::make_shared<NumberVariable>(1.0)},
        {"ping_timeout", std::make_shared<NumberVariable>(2.0)},
        {"is_ready", std::make_shared<BooleanVariable>(false)},
        {"last_message_age", std::make_shared<IntegerVariable>(0)},
        {"step_in_progress", std::make_shared<BooleanVariable>(false)},
    };
}

PlexusExpander::PlexusExpander(const std::string name,
                               ConstSerial_ptr const_serial,
                               const char id,
                               MessageHandler message_handler)
    : Module(plexus_expander, name),

      serial(std::const_pointer_cast<Serial>(const_serial)),
      expander_id(id),
      message_handler(message_handler) {
    this->properties = PlexusExpander::get_defaults();
    buffer_pos = 0;

    this->serial->enable_line_detection();
    this->serial->activate_external_mode();

    char buffer[256];
    int pos = csprintf(buffer, sizeof(buffer), "%c%ccore.print('__%c_READY__')", ID_TAG, expander_id, expander_id);
    this->serial->write_checked_line(buffer, pos);

    const int max_retries = 10;
    for (int i = 0; i < max_retries; i++) {
        echo("Debug: Waiting for READY response (attempt %d/%d)", i + 1, max_retries);
        this->handle_messages();
        delay(200); // Increased delay between retries
        this->serial->write_checked_line(buffer, pos);
        if (this->properties.at("is_ready")->boolean_value) {
            echo("Debug: Got READY response");
            return;
        }
    }
    echo("Warning: No READY response received");
}

void PlexusExpander::buffer_message(const char *message) {
    size_t msg_len = strlen(message);

    // If buffer is empty, add the ID tag and expander ID
    if (buffer_pos == 0) {
        message_buffer[0] = ID_TAG;
        message_buffer[1] = expander_id;
        buffer_pos = 2;
    } else {
        // Add semicolon separator if not the first command
        message_buffer[buffer_pos++] = ';';
    }

    // Check if we have enough space
    if (buffer_pos + msg_len < MSG_BUFFER_SIZE) {
        strcpy(&message_buffer[buffer_pos], message);
        buffer_pos += msg_len;
    } else {
        echo("Warning: Message buffer overflow, message dropped");
    }
}

void PlexusExpander::step() {
    if (this->properties.at("is_ready")->boolean_value) {
        if (buffer_pos > 0) {
            this->serial->write_checked_line(message_buffer, buffer_pos);
            buffer_pos = 0;
        }

        char buffer[256];
        int pos = csprintf(buffer, sizeof(buffer), "%c%ccore.run_step()", ID_TAG, expander_id);
        this->serial->write_checked_line(buffer, pos);
        this->properties.at("step_in_progress")->boolean_value = true;

        const unsigned long start_time = millis();
        const unsigned long timeout = 1000; // 1 second timeout

        while (this->properties.at("step_in_progress")->boolean_value && (millis() - start_time < timeout)) {
            this->handle_messages();
            delay(1);
        }

        if (this->properties.at("step_in_progress")->boolean_value) {
            echo("Warning: step_done not received within timeout");
        }
    }

    this->properties.at("last_message_age")->integer_value = millis_since(this->last_message_millis);
    Module::step();
}

void PlexusExpander::handle_messages() {
    static char buffer[1024];
    while (this->serial->has_buffered_lines()) {
        int len = this->serial->read_line(buffer, sizeof(buffer));
        check(buffer, len);

        // tag handling: msg looks like this: $9XXXXXXX
        if (buffer[0] == ID_TAG && buffer[1] == expander_id) {
            strcpy(buffer, buffer + 2);
        } else {
            echo("Debug: msg received. Tag and Id did not match");
            echo("Debug: buffer: %s", buffer);
            continue;
        }

        this->last_message_millis = millis();
        if (buffer[0] == '!' && buffer[1] == '!') {
            this->message_handler(&buffer[2], false, true);
        } else if (strstr(buffer, "__step_done__") != nullptr) {
            this->properties.at("step_in_progress")->boolean_value = false;
        } else if (strstr(buffer, "_READY__") != nullptr) {
            echo("plexus expander %c is ready", expander_id);
            this->properties.at("is_ready")->boolean_value = true;
        } else {
            echo("%s: %s", this->name.c_str(), buffer);
        }
    }
}

void PlexusExpander::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "run") {
        Module::expect(arguments, 1, string);
        std::string command = arguments[0]->evaluate_string();
        buffer_message(command.c_str());
    } else if (method_name == "restart") {
        Module::expect(arguments, 0);
        this->restart();
    } else {
        char buffer[1024];
        int pos = csprintf(buffer, sizeof(buffer), "core.%s(", method_name.c_str());
        pos += write_arguments_to_buffer(arguments, &buffer[pos], sizeof(buffer) - pos);
        pos += csprintf(&buffer[pos], sizeof(buffer) - pos, ")");
        buffer_message(buffer);
    }
}

void PlexusExpander::send_proxy(const std::string module_name, const std::string module_type, const std::vector<ConstExpression_ptr> arguments) {
    char buffer[256];
    int pos = csprintf(buffer, sizeof(buffer), "%s = %s(", module_name.c_str(), module_type.c_str());
    pos += write_arguments_to_buffer(arguments, &buffer[pos], sizeof(buffer) - pos);
    pos += csprintf(&buffer[pos], sizeof(buffer) - pos, "); ");
    pos += csprintf(&buffer[pos], sizeof(buffer) - pos, "%s.broadcast()", module_name.c_str());
    buffer_message(buffer);
}

void PlexusExpander::send_property(const std::string proxy_name, const std::string property_name, const ConstExpression_ptr expression) {
    char buffer[256];
    int pos = csprintf(buffer, sizeof(buffer), "%s.%s = ", proxy_name.c_str(), property_name.c_str());
    pos += expression->print_to_buffer(&buffer[pos], sizeof(buffer) - pos);
    buffer_message(buffer);
}

void PlexusExpander::send_call(const std::string proxy_name, const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    char buffer[256];
    int pos = csprintf(buffer, sizeof(buffer), "%s.%s(", proxy_name.c_str(), method_name.c_str());
    pos += write_arguments_to_buffer(arguments, &buffer[pos], sizeof(buffer) - pos);
    pos += csprintf(&buffer[pos], sizeof(buffer) - pos, ")");
    buffer_message(buffer);
}

bool PlexusExpander::is_ready() const {
    return this->properties.at("is_ready")->boolean_value;
}

std::string PlexusExpander::get_name() const {
    return this->name;
}

void PlexusExpander::restart() {
    char buffer[256];
    int pos = csprintf(buffer, sizeof(buffer), "%c%ccore.restart()", ID_TAG, expander_id);
    this->serial->write_checked_line(buffer, pos);
    this->properties.at("is_ready")->boolean_value = false;

    static char read_buffer[1024];
    const unsigned long boot_timeout = this->get_property("boot_timeout")->number_value * 1000;
    const unsigned long start_time = millis();

    while (!this->properties.at("is_ready")->boolean_value) {
        if (boot_timeout > 0 && millis_since(start_time) > boot_timeout) {
            echo("warning: plexus expander %c restart timed out", expander_id);
            break;
        }

        while (this->serial->has_buffered_lines()) {
            int len = this->serial->read_line(read_buffer, sizeof(read_buffer));
            check(read_buffer, len);

            if (read_buffer[0] == ID_TAG && read_buffer[1] == expander_id) {
                strcpy(read_buffer, read_buffer + 2);
            }

            echo("%s: %s", this->name.c_str(), read_buffer);
            if (strcmp("Ready.", read_buffer) == 0) {
                echo("plexus expander %c ready after restart", expander_id);
                break;
            }
        }

        if (strcmp("Ready.", read_buffer) == 0) {
            break;
        }

        delay(30);
    }

    this->serial->enable_line_detection();
    this->serial->activate_external_mode();

    buffer_pos = 0;
    pos = csprintf(buffer, sizeof(buffer), "%c%ccore.print('__%c_READY__')", ID_TAG, expander_id, expander_id);
    this->serial->write_checked_line(buffer, pos);

    const int max_retries = 10;
    for (int i = 0; i < max_retries; i++) {
        echo("Debug: Waiting for READY response after restart (attempt %d/%d)", i + 1, max_retries);
        this->handle_messages();
        delay(200);
        this->serial->write_checked_line(buffer, pos);
        if (this->properties.at("is_ready")->boolean_value) {
            echo("Debug: Got READY response after restart");
            return;
        }
    }
    echo("Warning: No READY response received after restart");
}