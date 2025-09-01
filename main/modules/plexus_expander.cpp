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
        {"is_ready", std::make_shared<BooleanVariable>(false)},
        {"last_message_age", std::make_shared<IntegerVariable>(0)},
        {"awaiting_step_completion", std::make_shared<BooleanVariable>(false)},
    };
}

PlexusExpander::PlexusExpander(const std::string name,
                               ConstSerial_ptr const_serial,
                               const uint8_t id,
                               MessageHandler message_handler)
    : Module(plexus_expander, name),
      serial(std::const_pointer_cast<Serial>(const_serial)),
      device_id(id),
      message_handler(message_handler) {
    this->properties = PlexusExpander::get_defaults();

    this->serial->enable_line_detection();
    this->serial->activate_external_mode();

    this->restart();
    const unsigned long boot_timeout = this->get_property("boot_timeout")->number_value * 1000;
    while (this->properties.at("is_ready")->boolean_value == false) {
        if (boot_timeout > 0 && millis_since(this->boot_start_time) > boot_timeout) {
            echo("warning: plexus expander with id %c connection timed out.", '0' + device_id);
            break;
        }
        this->check_boot_progress();
        delay(30);
    }
}

void PlexusExpander::queue_command(const char *command) {
    size_t cmd_len = strlen(command);

    if (buffer_pos == 0) {
        message_buffer[0] = ID_TAG;
        message_buffer[1] = '0' + device_id;
        buffer_pos = 2;
    } else {
        if (buffer_pos + 1 < MSG_BUFFER_SIZE) {
            message_buffer[buffer_pos++] = ';';
        }
    }

    if (buffer_pos + cmd_len < MSG_BUFFER_SIZE) {
        strcpy(&message_buffer[buffer_pos], command);
        buffer_pos += cmd_len;
    } else {
        echo("Warning: Message buffer overflow, command dropped");
    }
}

void PlexusExpander::flush_commands() {
    if (buffer_pos > 0) {
        this->serial->write_checked_line(message_buffer, buffer_pos);
        buffer_pos = 0;
    }
}

void PlexusExpander::step() {
    if (this->properties.at("is_ready")->boolean_value) {
        flush_commands();

        send_tagged_command("core.run_step()");
        this->properties.at("awaiting_step_completion")->boolean_value = true;

        const unsigned long start_time = millis();

        while (this->properties.at("awaiting_step_completion")->boolean_value && (millis() - start_time < STEP_TIMEOUT_MS)) {
            this->process_incoming_messages();
            delay(1);
        }

        if (this->properties.at("awaiting_step_completion")->boolean_value) {
            echo("Warning: step_done not received within timeout");
        }
    }

    this->properties.at("last_message_age")->integer_value = millis_since(this->last_message_time);
    Module::step();
}

void PlexusExpander::process_incoming_messages() {
    static char buffer[1024];
    while (this->serial->has_buffered_lines()) {
        int len = this->serial->read_line(buffer, sizeof(buffer));
        check(buffer, len);

        // Message format: $<id>COMMAND
        if (buffer[0] == ID_TAG && buffer[1] == '0' + device_id) {
            strcpy(buffer, buffer + 2);
        } else {
            echo("Plexus untagged message: %s", buffer);
            continue;
        }

        this->last_message_time = millis();
        if (buffer[0] == '!' && buffer[1] == '!') {
            this->message_handler(&buffer[2], false, true);
        } else if (strstr(buffer, "__step_done__") != nullptr) {
            this->properties.at("awaiting_step_completion")->boolean_value = false;
        } else {
            echo("%s: %s", this->name.c_str(), buffer);
        }
    }
}

void PlexusExpander::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "run") {
        Module::expect(arguments, 1, string);
        std::string command = arguments[0]->evaluate_string();
        queue_command(command.c_str());
    } else if (method_name == "restart") {
        Module::expect(arguments, 0);
        this->restart();
    } else {
        char buffer[1024];
        int pos = csprintf(buffer, sizeof(buffer), "core.%s(", method_name.c_str());
        pos += write_arguments_to_buffer(arguments, &buffer[pos], sizeof(buffer) - pos);
        pos += csprintf(&buffer[pos], sizeof(buffer) - pos, ")");
        queue_command(buffer);
    }
}

void PlexusExpander::send_proxy(const std::string module_name, const std::string module_type, const std::vector<ConstExpression_ptr> arguments) {
    char buffer[256];
    int pos = csprintf(buffer, sizeof(buffer), "%s = %s(", module_name.c_str(), module_type.c_str());
    pos += write_arguments_to_buffer(arguments, &buffer[pos], sizeof(buffer) - pos);
    pos += csprintf(&buffer[pos], sizeof(buffer) - pos, "); ");
    pos += csprintf(&buffer[pos], sizeof(buffer) - pos, "%s.broadcast()", module_name.c_str());
    queue_command(buffer);
}

void PlexusExpander::send_property(const std::string proxy_name, const std::string property_name, const ConstExpression_ptr expression) {
    char buffer[256];
    int pos = csprintf(buffer, sizeof(buffer), "%s.%s = ", proxy_name.c_str(), property_name.c_str());
    pos += expression->print_to_buffer(&buffer[pos], sizeof(buffer) - pos);
    queue_command(buffer);
}

void PlexusExpander::send_call(const std::string proxy_name, const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    char buffer[256];
    int pos = csprintf(buffer, sizeof(buffer), "%s.%s(", proxy_name.c_str(), method_name.c_str());
    pos += write_arguments_to_buffer(arguments, &buffer[pos], sizeof(buffer) - pos);
    pos += csprintf(&buffer[pos], sizeof(buffer) - pos, ")");
    queue_command(buffer);
}

bool PlexusExpander::is_ready() const {
    return this->properties.at("is_ready")->boolean_value;
}

std::string PlexusExpander::get_name() const {
    return this->name;
}

void PlexusExpander::check_boot_progress() {
    static char buffer[1024];
    while (this->serial->has_buffered_lines()) {
        int len = this->serial->read_line(buffer, sizeof(buffer));
        check(buffer, len);

        // Message format: $<id>COMMAND - check for tagged Ready message
        if (buffer[0] == ID_TAG && buffer[1] == '0' + device_id) {
            strcpy(buffer, buffer + 2);
        } else {
            // During boot, we might get non-tagged messages, just echo them
            echo("%s: %s", this->name.c_str(), buffer);
            continue;
        }

        this->last_message_time = millis();
        echo("%s: %s", this->name.c_str(), buffer);
        if (strcmp("Ready.", buffer) == 0) {
            this->properties.at("is_ready")->boolean_value = true;
            echo("plexus expander with id %c: Booting process completed successfully", '0' + device_id);
            break;
        }
    }
}

void PlexusExpander::restart() {
    send_tagged_command("core.restart()");
    this->boot_start_time = millis();
    this->properties.at("is_ready")->boolean_value = false;
    buffer_pos = 0;
}

std::string PlexusExpander::format_command(const std::string &command) {
    char buffer[256];
    int pos = csprintf(buffer, sizeof(buffer), "%c%c%s", ID_TAG, '0' + device_id, command.c_str());
    return std::string(buffer, pos);
}

void PlexusExpander::send_tagged_command(const std::string &command) {
    std::string tagged_command = format_command(command);
    this->serial->write_checked_line(tagged_command.c_str(), tagged_command.length());
}
