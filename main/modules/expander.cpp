#include "expander.h"

#include "module_helpers.h"
#include "serial.h"
#include "storage.h"
#include "utils/frame.h"
#include "utils/serial-replicator.h"
#include "utils/string_utils.h"
#include "utils/timing.h"
#include "utils/uart.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

static Module_ptr create_expander(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler message_handler) {
    if (arguments.size() != 1 && arguments.size() != 3) {
        throw std::runtime_error("unexpected number of arguments");
    }
    Module::expect(arguments, -1, identifier, integer, integer);
    const ConstSerial_ptr serial = get_module_argument<const Serial>(arguments[0]);
    const gpio_num_t boot_pin = arguments.size() > 1 ? (gpio_num_t)arguments[1]->evaluate_integer() : GPIO_NUM_NC;
    const gpio_num_t enable_pin = arguments.size() > 2 ? (gpio_num_t)arguments[2]->evaluate_integer() : GPIO_NUM_NC;
    return std::make_shared<Expander>(name, serial, boot_pin, enable_pin, message_handler);
}
REGISTER_MODULE(Expander, &create_expander)

const std::map<std::string, Variable_ptr> Expander::get_defaults() {
    return {
        {"boot_timeout", std::make_shared<NumberVariable>(5.0)},
        {"ping_interval", std::make_shared<NumberVariable>(1.0)},
        {"ping_timeout", std::make_shared<NumberVariable>(2.0)},
        {"is_ready", std::make_shared<BooleanVariable>(false)},
        {"last_message_age", std::make_shared<IntegerVariable>(0)},
        {"frames", std::make_shared<BooleanVariable>(false)},
        {"frame_errors", std::make_shared<IntegerVariable>(0)},
        {"frame_gaps", std::make_shared<IntegerVariable>(0)},
    };
}

static constexpr uint8_t PROXY_FRAME_ID = 255;           // reserved for the proxy frame
static constexpr unsigned long FRAME_FALLBACK_MS = 1000; // no proxy frame by then: the expander firmware has no frames
static constexpr unsigned long FRAME_GRACE_MS = 200;     // frames in flight may still carry the previous field list

Expander::Expander(const std::string name,
                   const ConstSerial_ptr serial,
                   const gpio_num_t boot_pin,
                   const gpio_num_t enable_pin,
                   MessageHandler message_handler)
    : Module(name),
      serial(serial),
      boot_pin(boot_pin),
      enable_pin(enable_pin),
      message_handler(message_handler) {

    this->properties = Expander::get_defaults();

    this->serial->claim(name);
    this->serial->enable_line_detection();
    if (boot_pin != GPIO_NUM_NC && enable_pin != GPIO_NUM_NC) {
        gpio_reset_pin(boot_pin);
        gpio_reset_pin(enable_pin);
        gpio_set_direction(boot_pin, GPIO_MODE_OUTPUT);
        gpio_set_direction(enable_pin, GPIO_MODE_OUTPUT);
        gpio_set_level(boot_pin, 1);
    }

    this->restart();
    const unsigned long boot_timeout = this->get_property("boot_timeout")->number_value * 1000;
    while (this->properties.at("is_ready")->boolean_value == false) {
        if (boot_timeout > 0 && millis_since(this->boot_start_time) > boot_timeout) {
            echo("warning: expander %s connection timed out.", this->name.c_str());
            // TODO: trigger error code
            break;
        }
        this->check_boot_progress();
        delay(30);
    }
}

void Expander::step() {
    if (this->properties.at("is_ready")->boolean_value) {
        this->ping();
        this->handle_messages();
        this->check_frames();
    } else {
        this->check_boot_progress();
    }
    this->properties.at("last_message_age")->integer_value = millis_since(this->last_message_millis);
    Module::step();
}

// lines from the expander are console lines of its own, so they can be as long as ours
static char line_buffer[CONSOLE_LINE_SIZE];

void Expander::check_boot_progress() {
    while (this->serial->has_buffered_lines()) {
        const int len = this->serial->read_line(line_buffer, sizeof(line_buffer));
        if (len < 0) {
            echo("%s: error while checking boot progress: %s", this->name.c_str(), Serial::read_line_error(len));
            continue;
        }
        bool checksum_ok = true;
        check(line_buffer, len, &checksum_ok);
        if (!checksum_ok) {
            echo("%s: Checksum mismatch while checking boot progress", this->name.c_str());
            continue;
        }
        this->last_message_millis = millis();
        echo("%s: %s", this->name.c_str(), line_buffer);
        if (strcmp("Ready.", line_buffer) == 0) {
            this->properties.at("is_ready")->boolean_value = true;
            echo("%s: Booting process completed successfully", this->name.c_str());
            break;
        }
    }
}

void Expander::ping() {
    const double last_message_age = this->get_property("last_message_age")->integer_value / 1000.0;
    const double ping_interval = this->get_property("ping_interval")->number_value;
    const double ping_timeout = this->get_property("ping_timeout")->number_value;
    if (!this->ping_pending) {
        if (last_message_age >= ping_interval) {
            this->serial->write_checked_line("core.print('__PONG__')");
            this->ping_pending = true;
        }
    } else {
        if (last_message_age >= ping_interval + ping_timeout) {
            echo("warning: expander %s connection lost", this->name.c_str());
            // TODO: trigger error code
            this->properties.at("is_ready")->boolean_value = false;
            this->ping_pending = false;
        }
    }
}

void Expander::restart() {
    this->ping_pending = false;
    if (this->boot_pin != GPIO_NUM_NC && this->enable_pin != GPIO_NUM_NC) {
        gpio_set_level(this->enable_pin, 0);
        delay(100);
        gpio_set_level(this->enable_pin, 1);
    } else {
        this->serial->write_checked_line("core.restart()");
    }
    this->serial->flush();
    this->boot_start_time = millis();
    this->properties.at("is_ready")->boolean_value = false;
}

void Expander::handle_messages(bool check_for_strapping_pins) {
    while (this->serial->has_buffered_lines()) {
        int len = this->serial->read_line(line_buffer, sizeof(line_buffer));
        if (len < 0) {
            echo("%s: error while handling messages: %s", this->name.c_str(), Serial::read_line_error(len));
            continue;
        }
        if (len > 0 && static_cast<uint8_t>(line_buffer[0]) == frame::BUS_MARKER) {
            this->handle_frame(line_buffer, len);
            this->last_message_millis = millis();
            this->ping_pending = false;
            continue;
        }
        bool checksum_ok = true;
        len = check(line_buffer, len, &checksum_ok);
        if (!checksum_ok) {
            echo("%s: Checksum mismatch while handling messages", this->name.c_str());
            this->last_message_millis = millis();
            this->ping_pending = false;
            continue;
        }
        if (check_for_strapping_pins) {
            this->check_strapping_pins(line_buffer);
        }
        this->last_message_millis = millis();
        this->ping_pending = false;
        if (line_buffer[0] == '!' && line_buffer[1] == '!') {
            this->message_handler(&line_buffer[2], false, true);
        } else if (strcmp("\"__PONG__\"", line_buffer) == 0) {
            // No echo for pong
        } else {
            echo("%s: %s", this->name.c_str(), line_buffer);
        }
    }
}

void Expander::handle_frame(const char *line, size_t length) {
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        --length;
    }
    static uint8_t body[frame::MAX_BODY];
    const size_t body_length = frame::bus_unstuff(line, length, body, sizeof(body));
    if (body_length == 0 || !frame::verify_body(body, body_length)) {
        this->properties.at("frame_errors")->integer_value++;
        return;
    }
    if (body[2] != PROXY_FRAME_ID) {
        frame::write_console(body, body_length); // a frame the expander defined itself goes on to the host
        return;
    }
    const uint8_t seq = body[3];
    if (this->frame_seq_valid && static_cast<uint8_t>(seq - this->frame_seq) != 1) {
        this->properties.at("frame_gaps")->integer_value += static_cast<uint8_t>(seq - this->frame_seq - 1);
    }
    this->frame_seq = seq;
    this->frame_seq_valid = true;
    const size_t payload_length = body[frame::HEADER_SIZE - 1];
    if (payload_length != this->frame_numeric_length + (this->frame_bit_count + 7) / 8) {
        if (millis_since(this->frame_defined_millis) > FRAME_GRACE_MS) {
            this->properties.at("frame_errors")->integer_value++;
        }
        return;
    }
    this->frame_seen = true;
    const uint8_t *payload = &body[frame::HEADER_SIZE];
    const uint8_t *bits = payload + this->frame_numeric_length;
    size_t pos = 0;
    size_t bit = 0;
    for (auto const &field : this->frame_fields) {
        if (field.type == '?') {
            field.variable->boolean_value = (bits[bit / 8] >> (bit % 8)) & 1;
            ++bit;
        } else if (field.type == 'i') {
            int32_t value;
            memcpy(&value, &payload[pos], 4);
            pos += 4;
            field.variable->integer_value = value;
        } else {
            float value;
            memcpy(&value, &payload[pos], 4);
            pos += 4;
            field.variable->number_value = value;
        }
    }
}

void Expander::check_frames() {
    if (!this->frame_fields.empty() && !this->frame_seen && millis_since(this->frame_defined_millis) > FRAME_FALLBACK_MS) {
        echo("warning: expander %s sends no proxy frames, falling back to text broadcasts", this->name.c_str());
        for (auto const &proxy_name : this->frame_proxies) {
            this->serial->write_checked_line((proxy_name + ".broadcast()").c_str());
        }
        this->frame_fields.clear();
        this->frame_proxies.clear();
        this->frame_numeric_length = 0;
        this->frame_bit_count = 0;
        this->properties.at("frames")->boolean_value = false;
    }
    const int64_t errors = this->properties.at("frame_errors")->integer_value;
    const int64_t gaps = this->properties.at("frame_gaps")->integer_value;
    if ((errors != this->reported_frame_errors || gaps != this->reported_frame_gaps) &&
        millis_since(this->frame_warning_millis) > 1000) {
        echo("warning: expander %s: %lld proxy frames missing, %lld corrupt", this->name.c_str(),
             gaps - this->reported_frame_gaps, errors - this->reported_frame_errors);
        this->reported_frame_errors = errors;
        this->reported_frame_gaps = gaps;
        this->frame_warning_millis = millis();
    }
}

void Expander::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "run") {
        Module::expect(arguments, 1, string);
        std::string command = arguments[0]->evaluate_string();
        this->serial->write_checked_line(command.c_str(), command.length());
    } else if (method_name == "restart") {
        Module::expect(arguments, 0);
        restart();
    } else if (method_name == "disconnect") {
        Module::expect(arguments, 0);
        this->serial->require_sole_user(this->name);
        deinstall();
    } else if (method_name == "flash") {
        if (arguments.size() > 1) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, boolean);
        bool force = arguments.size() > 0 ? arguments[0]->evaluate_boolean() : false;
        if (this->boot_pin == GPIO_NUM_NC || this->enable_pin == GPIO_NUM_NC) {
            throw std::runtime_error("expander \"" + this->name + "\" does not support flashing, pins not set");
        }
        this->serial->require_sole_user(this->name);
        gpio_set_level(this->boot_pin, 0);
        if (!force) {
            char command[32];
            for (const int pin : {0, BOOT_MODE_PIN, FLASH_VOLTAGE_PIN}) {
                const int length = csprintf(command, sizeof(command), "core.get_pin_status(%d)", pin);
                this->serial->write_checked_line(command, length);
            }
            delay(100);
            try {
                this->handle_messages(true);
            } catch (...) {
                gpio_set_level(this->boot_pin, 1);
                throw;
            }
        }
        deinstall();
        bool success = ZZ::Replicator::flashReplica(this->serial->uart_num,
                                                    this->enable_pin,
                                                    this->boot_pin,
                                                    this->serial->rx_pin,
                                                    this->serial->tx_pin,
                                                    this->serial->baud_rate);
        delay(100);
        this->serial->reinitialize_after_flash();
        if (!success) {
            throw std::runtime_error("could not flash expander \"" + this->name + "\"");
        } else {
            this->restart();
        }
    } else {
        static char buffer[1024];
        int pos = csprintf(buffer, sizeof(buffer), "core.%s(", method_name.c_str());
        pos += write_arguments_to_buffer(arguments, &buffer[pos], sizeof(buffer) - pos);
        pos += csprintf(&buffer[pos], sizeof(buffer) - pos, ")");
        this->serial->write_checked_line(buffer, pos);
    }
}

static bool strapping_pin_is_high(const char *buffer, const int pin) {
    char pattern[32];
    csprintf(pattern, sizeof(pattern), "GPIO_Status[%d]| Level: 1", pin);
    return strstr(buffer, pattern) != nullptr;
}

void Expander::check_strapping_pins(const char *buffer) {
    // These strapping pins are sampled at reset and are not directly controllable by the expander.
    if (strapping_pin_is_high(buffer, FLASH_VOLTAGE_PIN)) {
        echo("warning: GPIO%d state is HIGH, this can cause issues with flash voltage selection", FLASH_VOLTAGE_PIN);
    }
    if (strapping_pin_is_high(buffer, 0)) {
        throw std::runtime_error("GPIO0 current state is HIGH - must be LOW for boot mode");
    }
    if (strapping_pin_is_high(buffer, BOOT_MODE_PIN)) {
        char message[96];
        csprintf(message, sizeof(message),
                 "GPIO%d current state is HIGH - must be LOW or floating for flash mode", BOOT_MODE_PIN);
        throw std::runtime_error(message);
    }
}

void Expander::deinstall() {
    this->serial->deinstall();
    this->properties.at("is_ready")->boolean_value = false;
    if (this->boot_pin != GPIO_NUM_NC && this->enable_pin != GPIO_NUM_NC) {
        gpio_reset_pin(this->boot_pin);
        gpio_reset_pin(this->enable_pin);
        gpio_set_direction(this->boot_pin, GPIO_MODE_INPUT);
        gpio_set_direction(this->enable_pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(this->boot_pin, GPIO_FLOATING);
        gpio_set_pull_mode(this->enable_pin, GPIO_FLOATING);
    }
}

void Expander::send_proxy(const std::string module_name,
                          const std::string module_type,
                          const std::vector<ConstExpression_ptr> arguments,
                          const std::map<std::string, Variable_ptr> &properties) {
    static char buffer[1024];
    int pos = csprintf(buffer, sizeof(buffer), "%s = %s(", module_name.c_str(), module_type.c_str());
    pos += write_arguments_to_buffer(arguments, &buffer[pos], sizeof(buffer) - pos);
    pos += csprintf(&buffer[pos], sizeof(buffer) - pos, "); ");
    std::string fields;
    std::vector<proxy_frame_field_t> new_fields;
    size_t numeric_length = this->frame_numeric_length;
    size_t bit_count = this->frame_bit_count;
    bool framed = this->properties.at("frames")->boolean_value;
    for (auto const &[property_name, variable] : properties) {
        if (!framed) {
            break;
        }
        if (property_name == "is_ready") {
            continue; // the core's view of the expander, not a property of the remote module
        }
        const char type = variable->type == boolean ? '?' : variable->type == integer ? 'i'
                                                        : variable->type == number    ? 'f'
                                                                                      : 0;
        if (type == 0) {
            framed = false; // e.g. a string property: this proxy keeps its text broadcast
            break;
        }
        if (type == '?') {
            ++bit_count;
        } else {
            numeric_length += 4;
        }
        fields += (fields.empty() ? "" : " ") + module_name + "." + property_name + ":" + type;
        new_fields.push_back({variable, type});
    }
    framed = framed && !new_fields.empty() && numeric_length + (bit_count + 7) / 8 <= frame::MAX_PAYLOAD;
    if (framed) {
        if (this->frame_fields.empty()) {
            pos += csprintf(&buffer[pos], sizeof(buffer) - pos, "core.frame_lines = true; core.frame(%d, \"%s\", 0)",
                            PROXY_FRAME_ID, fields.c_str());
        } else {
            pos += csprintf(&buffer[pos], sizeof(buffer) - pos, "core.frame_add(%d, \"%s\")", PROXY_FRAME_ID, fields.c_str());
        }
        this->frame_fields.insert(this->frame_fields.end(), new_fields.begin(), new_fields.end());
        this->frame_proxies.push_back(module_name);
        this->frame_numeric_length = numeric_length;
        this->frame_bit_count = bit_count;
        this->frame_defined_millis = millis();
    } else {
        pos += csprintf(&buffer[pos], sizeof(buffer) - pos, "%s.broadcast()", module_name.c_str());
    }
    this->serial->write_checked_line(buffer, pos);
}

void Expander::send_property(const std::string proxy_name, const std::string property_name, const ConstExpression_ptr expression) {
    static char buffer[512];
    int pos = csprintf(buffer, sizeof(buffer), "%s.%s = ", proxy_name.c_str(), property_name.c_str());
    pos += expression->print_to_buffer(&buffer[pos], sizeof(buffer) - pos);
    this->serial->write_checked_line(buffer, pos);
}

void Expander::send_call(const std::string proxy_name, const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    static char buffer[512];
    int pos = csprintf(buffer, sizeof(buffer), "%s.%s(", proxy_name.c_str(), method_name.c_str());
    pos += write_arguments_to_buffer(arguments, &buffer[pos], sizeof(buffer) - pos);
    pos += csprintf(&buffer[pos], sizeof(buffer) - pos, ")");
    this->serial->write_checked_line(buffer, pos);
}
