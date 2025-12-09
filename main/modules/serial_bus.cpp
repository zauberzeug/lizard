#include "serial_bus.h"

#include "../utils/string_utils.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <stdexcept>

extern void process_line(const char *line, const int len);

static constexpr size_t FRAME_BUFFER_SIZE = 512;
static constexpr unsigned long POLL_TIMEOUT_MS = 250;
static constexpr size_t OUTGOING_QUEUE_LENGTH = 32;
static constexpr size_t INCOMING_QUEUE_LENGTH = 32;
static constexpr const char RESPONSE_PREFIX[] = "__BUS_RESPONSE__";
static constexpr const char POLL_CMD[] = "__POLL__";
static constexpr const char DONE_CMD[] = "__DONE__";

REGISTER_MODULE_DEFAULTS(SerialBus)

const std::map<std::string, Variable_ptr> SerialBus::get_defaults() {
    return {
        {"last_message_age", std::make_shared<IntegerVariable>(0)},
    };
}

SerialBus::SerialBus(const std::string &name, const ConstSerial_ptr serial, const uint8_t node_id)
    : Module(serial_bus, name), serial(serial), node_id(node_id) {
    this->properties = SerialBus::get_defaults();
    this->serial->enable_line_detection();

    if (!(this->outbound_queue = xQueueCreate(OUTGOING_QUEUE_LENGTH, sizeof(OutgoingMessage)))) {
        throw std::runtime_error("failed to create serial bus outbound queue");
    }
    if (!(this->inbound_queue = xQueueCreate(INCOMING_QUEUE_LENGTH, sizeof(IncomingMessage)))) {
        vQueueDelete(this->outbound_queue);
        throw std::runtime_error("failed to create serial bus inbound queue");
    }

    if (xTaskCreatePinnedToCore(
            SerialBus::communication_loop, "serial_bus_comm", 4096, this, 5, &this->communication_task, 1) != pdPASS) {
        vQueueDelete(this->outbound_queue);
        vQueueDelete(this->inbound_queue);
        throw std::runtime_error("failed to create serial bus communication task");
    }

    register_echo_callback([this](const char *line) { this->handle_echo(line); });
}

[[noreturn]] void SerialBus::communication_loop(void *param) {
    SerialBus *bus = static_cast<SerialBus *>(param);
    while (true) {
        bus->process_uart();
        if (bus->is_coordinator()) {
            // poll next peer
            if (!bus->is_polling && !bus->send_outgoing_queue()) {
                bus->poll_index = (bus->poll_index + 1) % bus->peer_ids.size();
                bus->send_message(bus->peer_ids[bus->poll_index], POLL_CMD, sizeof(POLL_CMD) - 1);
                bus->poll_start_millis = millis();
            }
            // handle poll timeout
            if (bus->is_polling && millis_since(bus->poll_start_millis) > POLL_TIMEOUT_MS) {
                bus->echo_queue("warning: serial bus %s poll to %u timed out", bus->name.c_str(), bus->peer_ids[bus->poll_index]);
                bus->is_polling = false;
            }
        } else {
            // respond to poll
            if (bus->requesting_node) {
                try {
                    bus->send_outgoing_queue();
                    bus->send_message(bus->requesting_node, DONE_CMD, sizeof(DONE_CMD) - 1);
                } catch (const std::exception &e) {
                    bus->echo_queue("warning: serial bus %s error during transmit window: %s", bus->name.c_str(), e.what());
                }
                bus->requesting_node = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void SerialBus::process_uart() {
    static char buffer[FRAME_BUFFER_SIZE];
    while (this->serial->has_buffered_lines()) {
        int len = 0;
        try {
            len = this->serial->read_line(buffer, sizeof(buffer));
            len = check(buffer, len);
        } catch (const std::runtime_error &e) {
            this->echo_queue("warning: serial bus %s dropped line: %s", this->name.c_str(), e.what());
            continue;
        }

        IncomingMessage message;
        if (!this->parse_message(buffer, message)) {
            this->echo_queue("warning: serial bus %s received malformed message: %s", this->name.c_str(), buffer);
            continue;
        }

        if (message.receiver != this->node_id) {
            continue;
        }

        if (message.length == 8 && std::strncmp(message.payload, POLL_CMD, message.length) == 0) {
            // Open transmit window for peer when coordinator polls
            if (!this->is_coordinator()) {
                this->requesting_node = message.sender;
            }
            continue;
        }
        if (message.length == 8 && std::strncmp(message.payload, DONE_CMD, message.length) == 0) {
            // Coordinator receives done signal from peer
            if (this->is_coordinator() && message.sender == this->peer_ids[this->poll_index]) {
                this->is_polling = false;
            }
            continue;
        }
        if (xQueueSend(this->inbound_queue, &message, 0) == pdTRUE) {
            this->last_message_millis = millis();
        } else {
            this->echo_queue("warning: serial bus %s inbound queue overflow", this->name.c_str());
        }
    }
}

void SerialBus::echo_queue(const char *format, ...) const {
    IncomingMessage message{this->node_id, this->node_id, 0, {}};
    va_list args;
    va_start(args, format);
    message.length = std::vsnprintf(message.payload, PAYLOAD_CAPACITY, format, args);
    va_end(args);
    xQueueSend(this->inbound_queue, &message, 0);
}

bool SerialBus::send_outgoing_queue() {
    bool sent = false;
    OutgoingMessage message;
    while (xQueueReceive(this->outbound_queue, &message, 0) == pdTRUE) {
        this->send_message(message.receiver, message.payload, message.length);
        sent = true;
    }
    return sent;
}

void SerialBus::step() {
    IncomingMessage message;
    while (xQueueReceive(this->inbound_queue, &message, 0) == pdTRUE) {
        this->handle_message(message);
    }
    this->properties.at("last_message_age")->integer_value = millis_since(this->last_message_millis);
    Module::step();
}

void SerialBus::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "send") {
        Module::expect(arguments, 2, integer, string);
        const int receiver = arguments[0]->evaluate_integer();
        if (receiver <= 0 || receiver >= 255) {
            throw std::runtime_error("receiver ID must be between 0 and 255");
        }
        const std::string payload = arguments[1]->evaluate_string();
        this->enqueue_message(static_cast<uint8_t>(receiver), payload.c_str(), payload.size());
    } else if (method_name == "make_coordinator") {
        if (arguments.empty()) {
            throw std::runtime_error("make_coordinator expects at least one peer ID");
        }
        std::vector<uint8_t> peers;
        peers.reserve(arguments.size());
        for (const auto &argument : arguments) {
            if ((argument->type & integer) == 0) {
                throw std::runtime_error("peer IDs must be integers");
            }
            const long peer_value = argument->evaluate_integer();
            if (peer_value <= 0 || peer_value >= 255) {
                throw std::runtime_error("peer IDs must be between 0 and 255");
            }
            peers.push_back(static_cast<uint8_t>(peer_value));
        }
        this->peer_ids = peers;
    } else if (method_name == "configure") {
        Module::expect(arguments, 2, integer, string);
        const int receiver = arguments[0]->evaluate_integer();
        if (receiver <= 0 || receiver >= 255) {
            throw std::runtime_error("receiver ID must be between 0 and 255");
        }
        const uint8_t target = static_cast<uint8_t>(receiver);
        const std::string script = arguments[1]->evaluate_string();
        this->enqueue_message(target, "!-", 2);

        std::string current;
        auto flush_line = [&]() {
            // Trim whitespace and send line if non-empty
            size_t start = 0;
            while (start < current.size() && std::isspace(static_cast<unsigned char>(current[start]))) {
                start++;
            }
            size_t end = current.size();
            while (end > start && std::isspace(static_cast<unsigned char>(current[end - 1]))) {
                end--;
            }
            if (end > start) {
                std::string cmd = "!+" + current.substr(start, end - start);
                this->enqueue_message(target, cmd.c_str(), cmd.size());
            }
            current.clear();
        };

        for (char c : script) {
            if (c == '\r') {
                continue;
            }
            if (c == '\n') {
                flush_line();
                continue;
            }
            if (c == ';') {
                current.push_back(';');
                flush_line();
                continue;
            }
            current.push_back(c);
        }
        flush_line();

        this->enqueue_message(target, "!.", 2);
        const char restart_cmd[] = "core.restart()";
        this->enqueue_message(target, restart_cmd, sizeof(restart_cmd) - 1);
    } else {
        Module::call(method_name, arguments);
    }
}

void SerialBus::enqueue_message(uint8_t receiver, const char *payload, size_t length) {
    if (length == 0) {
        throw std::runtime_error("payload must not be empty");
    }
    if (length >= PAYLOAD_CAPACITY) {
        throw std::runtime_error("payload is too large for serial bus");
    }
    for (size_t i = 0; i < length; ++i) {
        if (payload[i] == '\n') {
            throw std::runtime_error("payload must not contain newline characters");
        }
    }
    OutgoingMessage message{};
    message.receiver = receiver;
    message.length = length;
    memcpy(message.payload, payload, length);
    message.payload[length] = '\0';
    if (xQueueSend(this->outbound_queue, &message, pdMS_TO_TICKS(50)) != pdTRUE) {
        throw std::runtime_error("serial bus transmit queue is full");
    }
}

void SerialBus::send_message(uint8_t receiver, const char *payload, size_t length) const {
    static char buffer[FRAME_BUFFER_SIZE];
    const int header_len = csprintf(buffer, sizeof(buffer), "$$%u:%u$$", this->node_id, receiver);
    if (header_len < 0) {
        throw std::runtime_error("could not format bus header");
    }
    if (length + header_len >= sizeof(buffer)) {
        throw std::runtime_error("serial bus payload is too large");
    }
    memcpy(buffer + header_len, payload, length);
    this->serial->write_checked_line(buffer, header_len + length);
}

void SerialBus::handle_echo(const char *line) {
    // Only relay if target is set
    if (!line || this->echo_target_id == 0xff) {
        return;
    }
    char payload[PAYLOAD_CAPACITY];
    const int len = std::snprintf(payload, sizeof(payload), "%s:%s", RESPONSE_PREFIX, line);
    if (len < 0 || len >= static_cast<int>(sizeof(payload))) {
        echo("warning: serial bus %s output relay truncated", this->name.c_str());
        return;
    }
    try {
        this->enqueue_message(this->echo_target_id, payload, len);
    } catch (const std::runtime_error &e) {
        echo("warning: serial bus %s could not relay output: %s", this->name.c_str(), e.what());
    }
}

bool SerialBus::parse_message(const char *line, IncomingMessage &message) const {
    const std::string message_line(line);
    if (!starts_with(message_line, "$$")) {
        return false;
    }
    const size_t header_end = message_line.find("$$", 2);
    if (header_end == std::string::npos) {
        return false;
    }
    const std::string header = message_line.substr(2, header_end - 2);
    const size_t colon = header.find(':');
    if (colon == std::string::npos) {
        return false;
    }
    try {
        const int sender = std::stoi(header.substr(0, colon));
        const int receiver = std::stoi(header.substr(colon + 1));
        if (sender < 0 || sender > 255 || receiver < 0 || receiver > 255) {
            return false;
        }
        const size_t payload_len = message_line.size() - (header_end + 2);
        if (payload_len >= PAYLOAD_CAPACITY) {
            return false;
        }
        message.sender = static_cast<uint8_t>(sender);
        message.receiver = static_cast<uint8_t>(receiver);
        message.length = payload_len;
        memcpy(message.payload, message_line.c_str() + header_end + 2, payload_len);
        message.payload[payload_len] = '\0';
    } catch (...) {
        return false;
    }
    return true;
}

void SerialBus::handle_message(const IncomingMessage &message) {
    if (message.length == 0) {
        return;
    }

    // Log messages from communication task (sender == receiver == node_id)
    if (message.sender == this->node_id && message.receiver == this->node_id) {
        echo("%s", message.payload);
        return;
    }

    // Display output that was relayed back from a remote node
    const size_t prefix_len = sizeof(RESPONSE_PREFIX) - 1;
    if (message.length >= prefix_len && std::strncmp(message.payload, RESPONSE_PREFIX, prefix_len) == 0) {
        const char *msg = message.payload + prefix_len;
        size_t len = message.length - prefix_len;
        if (len > 0 && msg[0] == ':') {
            msg++;
            len--;
        }
        if (len > 0) {
            static char buffer[PAYLOAD_CAPACITY];
            const size_t copy_len = std::min(len, static_cast<size_t>(sizeof(buffer) - 1));
            memcpy(buffer, msg, copy_len);
            buffer[copy_len] = '\0';
            echo("bus[%u]: %s", message.sender, buffer);
        }
        return;
    }

    // Configuration commands starting with '!' are processed silently (no output relay)
    if (message.payload[0] == '!') {
        process_line(message.payload, message.length);
        return;
    }

    // Regular commands: process locally and relay any echo() output back to sender
    this->echo_target_id = message.sender;
    try {
        process_line(message.payload, message.length);
    } catch (const std::exception &e) {
        echo("%s", e.what());
    } catch (...) {
        echo("unknown error");
    }
    this->echo_target_id = 0xff;
}
