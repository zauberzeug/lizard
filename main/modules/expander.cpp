#include "expander.h"

#include "module_helpers.h"
#include "serial.h"
#include "storage.h"
#include "utils/serial-replicator.h"
#include "utils/string_utils.h"
#include "utils/timing.h"
#include "utils/uart.h"
#include "global.h"
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
        // -1: proxies broadcast their properties as text; otherwise they come as frames, every this many ms (0: every step)
        {"telemetry_interval", std::make_shared<IntegerVariable>(-1)},
        {"telemetry_frames", std::make_shared<IntegerVariable>(0)},
        {"telemetry_errors", std::make_shared<IntegerVariable>(0)},
        {"telemetry_gaps", std::make_shared<IntegerVariable>(0)},
        {"telemetry_mismatch", std::make_shared<IntegerVariable>(0)},
    };
}

static constexpr unsigned long TELEMETRY_FALLBACK_MS = 5000;   // no layout by then: the expander firmware has no frames
static constexpr unsigned long TELEMETRY_ORDER_DELAY_MS = 200; // quiet time after the last new proxy before ordering

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
    this->telemetry_frames = this->properties.at("telemetry_frames");
    this->telemetry_errors = this->properties.at("telemetry_errors");
    this->telemetry_gaps = this->properties.at("telemetry_gaps");
    this->telemetry_mismatch = this->properties.at("telemetry_mismatch");

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
    const unsigned long boot_timeout = this->get_property("boot_timeout")->number_value() * 1000;
    while (this->properties.at("is_ready")->boolean_value() == false) {
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
    if (this->properties.at("is_ready")->boolean_value()) {
        this->ping();
        this->handle_messages();
        this->check_telemetry();
    } else if (!this->disconnected) {
        this->check_boot_progress();
    }
    this->properties.at("last_message_age")->set_integer_value(millis_since(this->last_message_millis));
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
            this->handle_ready();
            break;
        }
    }
}

void Expander::handle_ready() {
    this->properties.at("is_ready")->set_boolean_value(true);
    echo("%s: Booting process completed successfully", this->name.c_str());
    if (!this->proxies.empty()) {
        // The other microcontroller boots with an empty module table; proxies are set up only once, when they are created (#117).
        this->set_proxies_not_ready();
        echo("%s: proxies created before this boot need a core restart (%d)", this->name.c_str(), (int)this->proxies.size());
    }
}

void Expander::set_not_ready() {
    this->properties.at("is_ready")->set_boolean_value(false);
    this->set_proxies_not_ready();
}

void Expander::ping() {
    const double last_message_age = this->get_property("last_message_age")->integer_value() / 1000.0;
    const double ping_interval = this->get_property("ping_interval")->number_value();
    const double ping_timeout = this->get_property("ping_timeout")->number_value();
    if (!this->ping_pending) {
        if (last_message_age >= ping_interval) {
            this->serial->write_checked_line("core.print('__PONG__')");
            this->ping_pending = true;
        }
    } else {
        if (last_message_age >= ping_interval + ping_timeout) {
            echo("warning: expander %s connection lost", this->name.c_str());
            // TODO: trigger error code
            this->set_not_ready();
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
    this->set_not_ready();
}

void Expander::set_proxies_not_ready() {
    for (Module *proxy : this->proxies) {
        proxy->get_property("is_ready")->set_boolean_value(false);
    }
}

void Expander::handle_messages(bool check_for_strapping_pins) {
    while (this->serial->has_buffered_lines()) {
        int len = this->serial->read_line(line_buffer, sizeof(line_buffer));
        if (len < 0) {
            echo("%s: error while handling messages: %s", this->name.c_str(), Serial::read_line_error(len));
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
        if (line_buffer[0] == telemetry::FRAME_PREFIX ||
            strncmp(line_buffer, telemetry::LAYOUT_PREFIX, telemetry::LAYOUT_PREFIX_LENGTH) == 0) {
            this->handle_telemetry_line(line_buffer, len);
        } else if (line_buffer[0] == '!' && line_buffer[1] == '!') {
            this->message_handler(&line_buffer[2], false, true);
        } else if (strcmp("\"__PONG__\"", line_buffer) == 0) {
            // No echo for pong
        } else if (strcmp("Ready.", line_buffer) == 0) {
            // The other microcontroller rebooted on its own (panic, watchdog, brown-out, EN button, core.restart() via run()),
            // usually faster than the ping timeout, so the connection never counted as lost.
            echo("%s: %s", this->name.c_str(), line_buffer);
            this->handle_ready();
        } else {
            echo("%s: %s", this->name.c_str(), line_buffer);
        }
    }
}

void Expander::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "run") {
        Module::expect(arguments, 1, string);
        this->require_connected();
        std::string command = arguments[0]->evaluate_string();
        this->serial->write_checked_line(command.c_str(), command.length());
    } else if (method_name == "restart") {
        Module::expect(arguments, 0);
        this->require_connected();
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
        if (!force && this->disconnected) {
            throw std::runtime_error("expander \"" + this->name + "\" is disconnected, strapping pins cannot be checked, use flash(true)");
        }
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
        bool success = ZZ::Replicator::flash_replica(this->serial->uart_num,
                                                     this->enable_pin,
                                                     this->boot_pin,
                                                     this->serial->rx_pin,
                                                     this->serial->tx_pin,
                                                     this->serial->baud_rate);
        delay(100);
        this->serial->reinitialize_after_flash();
        this->disconnected = false;
        if (!success) {
            throw std::runtime_error("could not flash expander \"" + this->name + "\"");
        } else {
            this->restart();
        }
    } else {
        this->require_connected();
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

void Expander::require_connected() const {
    if (this->disconnected) {
        throw std::runtime_error("expander \"" + this->name + "\" is disconnected, use flash(true) to reconnect");
    }
}

void Expander::deinstall() {
    this->serial->deinstall();
    this->disconnected = true;
    this->set_not_ready();
    if (this->boot_pin != GPIO_NUM_NC && this->enable_pin != GPIO_NUM_NC) {
        gpio_reset_pin(this->boot_pin);
        gpio_reset_pin(this->enable_pin);
        gpio_set_direction(this->boot_pin, GPIO_MODE_INPUT);
        gpio_set_direction(this->enable_pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(this->boot_pin, GPIO_FLOATING);
        gpio_set_pull_mode(this->enable_pin, GPIO_FLOATING);
    }
}

void Expander::add_proxy(Module *proxy) {
    this->proxies.push_back(proxy);
}

void Expander::send_proxy(const std::string module_name, const std::string module_type, const std::vector<ConstExpression_ptr> arguments) {
    static char buffer[512];
    int pos = csprintf(buffer, sizeof(buffer), "%s = %s(", module_name.c_str(), module_type.c_str());
    pos += write_arguments_to_buffer(arguments, &buffer[pos], sizeof(buffer) - pos);
    pos += csprintf(&buffer[pos], sizeof(buffer) - pos, "); ");
    if (this->properties.at("telemetry_interval")->integer_value() >= 0) {
        // one order for all proxies, once no new one has come for TELEMETRY_ORDER_DELAY_MS
        this->telemetry_proxies.push_back({module_name, module_type});
        this->telemetry_order_pending = true;
        this->telemetry_proxy_millis = millis();
        pos -= 2; // no broadcast: drop the "; "
    } else {
        pos += csprintf(&buffer[pos], sizeof(buffer) - pos, "%s.broadcast()", module_name.c_str());
    }
    this->serial->write_checked_line(buffer, pos);
}

void Expander::count(const Variable_ptr &counter, const int64_t increment) {
    counter->set_integer_value(counter->integer_value() + increment);
}

void Expander::send_telemetry_orders() {
    // the fields of all proxies, as few frames as the payload limit allows; the expander names them like we do
    const int64_t interval = this->properties.at("telemetry_interval")->integer_value();
    std::vector<std::string> lines;
    std::string line;
    size_t bytes = 0;
    size_t bits = 0;
    for (auto const &[proxy_name, proxy_type] : this->telemetry_proxies) {
        for (auto const &[property_name, variable] : Module::get_module_defaults(proxy_type)) {
            if (property_name == "is_ready" || (variable->type != boolean && variable->type != integer && variable->type != number)) {
                continue;
            }
            const size_t size = telemetry::field_size(telemetry::type_for(variable));
            const bool is_bit = variable->type == boolean;
            const size_t payload = bytes + size + (bits + (is_bit ? 1 : 0) + 7) / 8;
            if (!line.empty() && (payload > telemetry::MAX_PAYLOAD || line.size() > 1800)) {
                lines.push_back(line);
                line.clear();
                bytes = 0;
                bits = 0;
            }
            line += (line.empty() ? "core.telemetry(" : ", ") + proxy_name + "." + property_name;
            bytes += size;
            bits += is_bit ? 1 : 0;
        }
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    this->serial->write_checked_line("core.clear_telemetry()");
    for (auto &order : lines) {
        order += ", " + std::to_string(interval) + ")";
        this->serial->write_checked_line(order.c_str(), order.size());
    }
    this->telemetry_layout.clear();
    this->telemetry_mapped.clear();
    this->telemetry_slots.clear();
    this->telemetry_last_seq.clear(); // the new frames count from anew
    this->telemetry_layout_seen = false;
    this->telemetry_order_millis = millis();
    this->telemetry_layout_request_millis = millis(); // frames of the old order still arriving need no layout request
    this->telemetry_order_pending = false;
}

void Expander::check_telemetry() {
    if (this->telemetry_order_pending) {
        // proxies created one by one at the console get one order, not one per proxy
        if (millis_since(this->telemetry_proxy_millis) > TELEMETRY_ORDER_DELAY_MS) {
            this->send_telemetry_orders();
        }
        return; // the fallback waits for an order that was sent
    }
    if (!this->telemetry_proxies.empty() && !this->telemetry_layout_seen &&
        millis_since(this->telemetry_order_millis) > TELEMETRY_FALLBACK_MS) {
        echo("warning: expander %s sends no telemetry layout, falling back to text broadcasts", this->name.c_str());
        this->serial->write_checked_line("core.clear_telemetry()"); // in case the order only came late
        for (auto const &[proxy_name, proxy_type] : this->telemetry_proxies) {
            this->serial->write_checked_line((proxy_name + ".broadcast()").c_str());
        }
        this->telemetry_proxies.clear();
        this->properties.at("telemetry_interval")->set_integer_value(-1);
    }
}

void Expander::handle_telemetry_line(const char *line, const int length) {
    if (line[0] != telemetry::FRAME_PREFIX) {
        telemetry::LayoutLine layout_line;
        if (!telemetry::parse_layout(line, length, layout_line)) {
            this->count(this->telemetry_errors);
            return;
        }
        if (layout_line.version != telemetry::FORMAT_VERSION) {
            if (!this->telemetry_version_reported) {
                echo("warning: expander %s sends telemetry format v%d, this node reads v%d", this->name.c_str(), layout_line.version,
                     telemetry::FORMAT_VERSION);
                this->telemetry_version_reported = true;
            }
            return;
        }
        this->telemetry_layout_seen = true;
        this->telemetry_layout.set(layout_line);
        // "proxy.property": the proxy's own variable receives the field
        const size_t dot = layout_line.name.find('.');
        const std::string proxy_name = dot == std::string::npos ? "" : layout_line.name.substr(0, dot);
        Variable *variable = nullptr;
        if (std::any_of(this->telemetry_proxies.begin(), this->telemetry_proxies.end(),
                        [&](const auto &proxy) { return proxy.first == proxy_name; })) {
            const Variable_ptr property = Global::get_module(proxy_name)->get_property(layout_line.name.substr(dot + 1));
            // a property of another type keeps its value; the rest of the frame is still mapped
            variable = telemetry::type_matches(*property, layout_line.type) ? property.get() : nullptr;
        } else if (!this->telemetry_mapped.count(layout_line.frame_id)) {
            return; // a frame without proxy fields
        }
        // every line of a mapped frame sets its field anew, so a frame id given to other fields keeps no old mapping
        std::vector<Variable *> &variables = this->telemetry_mapped[layout_line.frame_id];
        if (layout_line.count > 0 && variables.size() > layout_line.count) {
            variables.resize(layout_line.count);
        }
        if (variables.size() <= layout_line.index) {
            variables.resize(layout_line.index + 1, nullptr);
        }
        variables[layout_line.index] = variable;
        this->telemetry_slots[layout_line.frame_id] =
            telemetry::map_frame(this->telemetry_layout.frames[layout_line.frame_id], variables);
        return;
    }
    static uint8_t body[telemetry::MAX_BODY + 3];
    const size_t body_length = telemetry::decode_line(line, length, body, sizeof(body));
    if (body_length == 0) {
        this->count(this->telemetry_errors);
        return;
    }
    this->count(this->telemetry_frames);
    const uint8_t frame_id = body[0];
    const uint8_t seq = body[1];
    const int expected = this->telemetry_layout.expected_payload(frame_id);
    if (expected < 0) {
        // a layout line was lost on the way: ask for the layout again, as the bus does
        if (!this->telemetry_proxies.empty() && millis_since(this->telemetry_layout_request_millis) > 2000) {
            this->telemetry_layout_request_millis = millis();
            this->serial->write_checked_line("core.telemetry_info()");
        }
        return;
    }
    if (static_cast<size_t>(expected) != body_length - telemetry::HEADER_SIZE - telemetry::CRC_SIZE) {
        this->count(this->telemetry_mismatch);
        return;
    }
    // sequence numbers only with a known layout, so frames of an old order do not start the count
    const auto last = this->telemetry_last_seq.find(frame_id);
    if (last != this->telemetry_last_seq.end() && static_cast<uint8_t>(seq - last->second - 1) < 128) {
        this->count(this->telemetry_gaps, static_cast<uint8_t>(seq - last->second - 1));
    }
    this->telemetry_last_seq[frame_id] = seq;
    const auto slots = this->telemetry_slots.find(frame_id);
    if (slots != this->telemetry_slots.end()) {
        telemetry::apply(slots->second, &body[telemetry::HEADER_SIZE]);
    }
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
