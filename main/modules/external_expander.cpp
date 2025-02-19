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
        {"boot_timeout", std::make_shared<NumberVariable>(5.0)},
        {"ping_interval", std::make_shared<NumberVariable>(1.0)},
        {"ping_timeout", std::make_shared<NumberVariable>(2.0)},
        {"is_ready", std::make_shared<BooleanVariable>(false)},
        {"last_message_age", std::make_shared<IntegerVariable>(0)},
    };
}

ExternalExpander::ExternalExpander(const std::string name,
                                   const ConstSerial_ptr serial,
                                   const char *expander_id,
                                   MessageHandler message_handler)
    : Module(external_expander, name),
      serial(serial),
      expander_id(expander_id),
      message_handler(message_handler) {

    this->properties = ExternalExpander::get_defaults();

    this->serial->enable_line_detection();

    // cheeck if external expander is answering
    char buffer[256];
    int pos = csprintf(buffer, sizeof(buffer), "@%s:core.print('__%s_READY__')", expander_id, expander_id);
    this->serial->write_checked_line(buffer, pos);
    // check if pong is received manually
    this->handle_messages();
}

void ExternalExpander::step() {
    if (this->properties.at("is_ready")->boolean_value) {
        this->ping();

        char buffer[256];
        int pos = csprintf(buffer, sizeof(buffer), "@%s:core.run_step()", this->expander_id);
        this->serial->write_checked_line(buffer, pos);

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
        } else if (strstr(buffer, ("__" + std::string(this->expander_id) + "_PONG__").c_str()) != nullptr) {
            // No echo for pong
        } else if (strstr(buffer, ("__" + std::string(this->expander_id) + "_READY__").c_str()) != nullptr) {
            echo("external expander %s is ready", this->expander_id);
            this->properties.at("is_ready")->boolean_value = true;
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
            char buffer[256];
            int pos = csprintf(buffer, sizeof(buffer), "@%s:core.print('__%s_PONG__')", this->expander_id, this->expander_id);
            this->serial->write_checked_line(buffer, pos);
            this->ping_pending = true;
        }
    } else {
        if (last_message_age >= ping_interval + ping_timeout) {
            echo("warning: external expander connection lost");
            this->properties.at("is_ready")->boolean_value = false;
        }
    }
}

void ExternalExpander::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "run") {
        Module::expect(arguments, 1, string);
        std::string command = arguments[0]->evaluate_string();
        char buffer[1024];
        int pos = csprintf(buffer, sizeof(buffer), "@%s:%s", this->expander_id, command.c_str());
        this->serial->write_checked_line(buffer, pos);
    } else if (method_name == "restart") {
        Module::expect(arguments, 0);
        echo("restarting not supported for external expander right now");
        // restart();
    } else {
        static char buffer[1024];
        int pos = csprintf(buffer, sizeof(buffer), "@%s:core.%s(", this->expander_id, method_name.c_str());
        pos += write_arguments_to_buffer(arguments, &buffer[pos], sizeof(buffer) - pos);
        pos += csprintf(&buffer[pos], sizeof(buffer) - pos, ")");
        this->serial->write_checked_line(buffer, pos);
    }
}

void ExternalExpander::send_proxy(const std::string module_name, const std::string module_type, const std::vector<ConstExpression_ptr> arguments) {
    static char buffer[256];
    int pos = csprintf(buffer, sizeof(buffer), "@%s:%s = %s(", this->expander_id, module_name.c_str(), module_type.c_str());
    pos += write_arguments_to_buffer(arguments, &buffer[pos], sizeof(buffer) - pos);
    pos += csprintf(&buffer[pos], sizeof(buffer) - pos, ")");
    pos += csprintf(&buffer[pos], sizeof(buffer) - pos, "%s.broadcast()", module_name.c_str());
    this->serial->write_checked_line(buffer, pos);
}

void ExternalExpander::send_property(const std::string proxy_name, const std::string property_name, const ConstExpression_ptr expression) {
    static char buffer[256];
    int pos = csprintf(buffer, sizeof(buffer), "@%s:%s.%s = ", this->expander_id, proxy_name.c_str(), property_name.c_str());
    pos += expression->print_to_buffer(&buffer[pos], sizeof(buffer) - pos);
    this->serial->write_checked_line(buffer, pos);
}

void ExternalExpander::send_call(const std::string proxy_name, const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    static char buffer[256];
    int pos = csprintf(buffer, sizeof(buffer), "@%s:%s.%s(", this->expander_id, proxy_name.c_str(), method_name.c_str());
    pos += write_arguments_to_buffer(arguments, &buffer[pos], sizeof(buffer) - pos);
    pos += csprintf(&buffer[pos], sizeof(buffer) - pos, ")");
    this->serial->write_checked_line(buffer, pos);
}
