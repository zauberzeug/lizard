#include "external_expander.h"
#include "utils/string_utils.h"
#include "utils/timing.h"
#include "utils/uart.h"
#include <cstdlib>
#include <cstring>
#include <stdexcept>

REGISTER_MODULE_DEFAULTS(ExternalExpander)

const std::map<std::string, Variable_ptr> ExternalExpander::get_defaults() {
    return {
        {"ping_interval", std::make_shared<NumberVariable>(1.0)},
        {"ping_timeout", std::make_shared<NumberVariable>(2.0)},
        {"is_ready", std::make_shared<BooleanVariable>(false)},
        {"last_message_age", std::make_shared<IntegerVariable>(0)},
    };
}

ExternalExpander::ExternalExpander(const std::string name, const ConstSerial_ptr serial)
    : Module(external_expander, name), serial(serial) {
    this->properties = ExternalExpander::get_defaults();
    this->serial->enable_line_detection();
}

void ExternalExpander::step() {
    if (this->properties.at("is_ready")->boolean_value) {
        this->ping();

        // Request step from all registered expanders
        for (const auto &[id, name] : expander_names) {
            this->serial->write_checked_line("@%d:core.run_step()", id);
        }

        this->handle_messages();
    }
    this->properties.at("last_message_age")->integer_value = millis_since(this->last_message_millis);
    Module::step();
}

void ExternalExpander::handle_messages() {
    static char buffer[1024];
    while (this->serial->has_buffered_lines()) {
        int len = this->serial->read_line(buffer, sizeof(buffer));
        check(buffer, len);
        this->last_message_millis = millis();
        this->ping_pending = false;

        if (buffer[0] == '!' && buffer[1] == '!') {
            // Property update from expander
            // Format: !!property_name=value
            char *equals = strchr(buffer + 2, '=');
            if (equals) {
                *equals = 0;
                std::string property_name = buffer + 2;
                std::string property_value = equals + 1;

                // Update property in manager
                if (!this->properties.count(property_name)) {
                    // Create property if it doesn't exist
                    this->properties[property_name] = std::make_shared<StringVariable>();
                }
                this->properties[property_name]->string_value = property_value;
            }
        } else if (strcmp("\"__PONG__\"", buffer) == 0) {
            // No echo for pong
        } else {
            echo("%s: %s", this->name.c_str(), buffer);
        }
    }
}

void ExternalExpander::ping() {
    const double last_message_age = this->get_property("last_message_age")->integer_value / 1000.0;
    const double ping_interval = this->get_property("ping_interval")->number_value;
    const double ping_timeout = this->get_property("ping_timeout")->number_value;

    if (!this->ping_pending) {
        if (last_message_age >= ping_interval) {
            // Ping all expanders
            for (const auto &[id, name] : expander_names) {
                this->serial->write_checked_line("@%d:core.print('__PONG__')", id);
            }
            this->ping_pending = true;
        }
    } else {
        if (last_message_age >= ping_interval + ping_timeout) {
            echo("warning: external expander connection lost");
            this->properties.at("is_ready")->boolean_value = false;
        }
    }
}

void ExternalExpander::register_expander(uint8_t id, const std::string &name) {
    expander_names[id] = name;
    echo("Registered external expander %s with ID %d", name.c_str(), id);
}

void ExternalExpander::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "register") {
        Module::expect(arguments, 2, integer, string);
        uint8_t id = arguments[0]->evaluate_integer();
        std::string name = arguments[1]->evaluate_string();
        register_expander(id, name);
    } else if (method_name == "send") {
        Module::expect(arguments, 2, integer, string);
        uint8_t id = arguments[0]->evaluate_integer();
        std::string command = arguments[1]->evaluate_string();
        if (expander_names.count(id)) {
            char buffer[256];
            int pos = csprintf(buffer, sizeof(buffer), "@%d:%s", id, command.c_str());
            this->serial->write_checked_line(buffer, pos);
        } else {
            throw std::runtime_error("unknown expander id");
        }
    } else {
        Module::call(method_name, arguments);
    }
}