#include "serial_bus.h"

#include "../utils/string_utils.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "../utils/ota.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <stdexcept>
extern void process_line(const char *line, const int len);

namespace {
constexpr size_t FRAME_BUFFER_SIZE = 512;
constexpr uint8_t BROADCAST_ID = 0xff;
constexpr unsigned long POLL_TIMEOUT_MS = 250;
constexpr size_t OUTGOING_QUEUE_LENGTH = 32;
constexpr size_t INCOMING_QUEUE_LENGTH = 32;
constexpr const char RESPONSE_PREFIX[] = "__BUS_RESPONSE__";

struct ResponseContext {
    SerialBus *bus;
    uint8_t receiver;
    bool sent_line = false;
};

std::string trim_copy(const std::string &value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        start++;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        end--;
    }
    return value.substr(start, end - start);
}
} // namespace

REGISTER_MODULE_DEFAULTS(SerialBus)

const std::map<std::string, Variable_ptr> SerialBus::get_defaults() {
    return {
        {"is_coordinator", std::make_shared<BooleanVariable>(false)},
        {"peer_count", std::make_shared<IntegerVariable>(0)},
        {"last_message_age", std::make_shared<IntegerVariable>(0)},
    };
}

SerialBus::SerialBus(const std::string &name,
                     const ConstSerial_ptr serial,
                     const uint8_t node_id,
                     MessageHandler message_handler)
    : Module(serial_bus, name),
      serial(serial),
      node_id(node_id),
      message_handler(message_handler) {
    this->properties = SerialBus::get_defaults();
    this->properties.at("is_coordinator")->boolean_value = this->is_coordinator();
    this->properties.at("peer_count")->integer_value = this->peer_ids.size();
    this->serial->enable_line_detection();
    this->last_message_millis = millis();

    this->outbound_queue = xQueueCreate(OUTGOING_QUEUE_LENGTH, sizeof(OutgoingMessage));
    this->inbound_queue = xQueueCreate(INCOMING_QUEUE_LENGTH, sizeof(BusFrame));
    if (!this->outbound_queue || !this->inbound_queue) {
        throw std::runtime_error("failed to create serial bus queues");
    }
    this->start_communicator();
}

bool SerialBus::is_coordinator() const {
    return this->coordinator;
}

void SerialBus::configure_coordinator(const std::vector<uint8_t> &peers) {
    this->coordinator = true;
    this->peer_ids = peers;
    this->poll_index = 0;
    this->waiting_for_done = false;
    this->current_poll_target = 0;
    this->properties.at("is_coordinator")->boolean_value = true;
    this->properties.at("peer_count")->integer_value = this->peer_ids.size();
}

void SerialBus::start_communicator() {
    BaseType_t result = xTaskCreatePinnedToCore(
        SerialBus::communicator_task_trampoline,
        "serial_bus_comm",
        4096,
        this,
        5,
        &this->communicator_task,
        1);
    if (result != pdPASS) {
        throw std::runtime_error("failed to create serial bus communicator task");
    }
}

void SerialBus::communicator_task_trampoline(void *param) {
    SerialBus *bus = static_cast<SerialBus *>(param);
    bus->communicator_loop();
    configASSERT(!"SerialBus::communicator_loop returned unexpectedly");
}

[[noreturn]] void SerialBus::communicator_loop() {
    for (;;) {
        this->communicator_process_uart();
        if (this->is_coordinator()) {
            if (!this->waiting_for_done) {
                if (!this->flush_outgoing_queue()) {
                    this->coordinator_poll_step();
                }
            } else {
                this->check_poll_timeout();
            }
        } else if (this->transmit_window_open) {
            this->flush_outgoing_queue();
            this->send_done(this->window_requester);
            this->transmit_window_open = false;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void SerialBus::communicator_process_uart() {
    static char buffer[FRAME_BUFFER_SIZE];
    while (this->serial->has_buffered_lines()) {
        int len = 0;
        try {
            len = this->serial->read_line(buffer, sizeof(buffer));
            len = check(buffer, len);
        } catch (const std::runtime_error &e) {
            echo("warning: serial bus %s dropped line: %s", this->name.c_str(), e.what());
            continue;
        }

        BusFrame frame;
        if (!this->parse_frame(buffer, frame)) {
            echo("warning: serial bus %s received malformed frame: %s", this->name.c_str(), buffer);
            continue;
        }

        if (frame.receiver != this->node_id && frame.receiver != BROADCAST_ID) {
            continue;
        }

        if (frame.length == 8 && std::strncmp(frame.payload, "__POLL__", frame.length) == 0) {
            this->handle_poll_request(frame.sender);
            continue;
        }
        if (frame.length == 8 && std::strncmp(frame.payload, "__DONE__", frame.length) == 0) {
            this->handle_done(frame.sender);
            continue;
        }
        this->push_incoming_frame(frame);
    }
}

bool SerialBus::flush_outgoing_queue() {
    bool sent = false;
    OutgoingMessage message;
    while (xQueueReceive(this->outbound_queue, &message, 0) == pdTRUE) {
        this->send_frame(message.receiver, message.payload, message.length);
        sent = true;
    }
    return sent;
}

void SerialBus::push_incoming_frame(const BusFrame &frame) {
    if (xQueueSend(this->inbound_queue, &frame, 0) != pdTRUE) {
        echo("warning: serial bus %s inbound queue overflow", this->name.c_str());
    } else {
        this->last_message_millis = millis();
    }
}

void SerialBus::drain_inbox() {
    BusFrame frame;
    while (xQueueReceive(this->inbound_queue, &frame, 0) == pdTRUE) {
        this->handle_frame(frame);
    }
}

void SerialBus::step() {
    this->drain_inbox();

    ota::bus_tick(this->ota_session, millis(), this->name.c_str(),
                  [this](uint8_t receiver, const char *payload, size_t length) {
                      this->enqueue_message(receiver, payload, length);
                  });

    this->properties.at("last_message_age")->integer_value = millis_since(this->last_message_millis);
    Module::step();
}

void SerialBus::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "send") {
        Module::expect(arguments, 2, integer, string);
        const int receiver = arguments[0]->evaluate_integer();
        if (receiver < 0 || receiver > 255) {
            throw std::runtime_error("receiver id must be between 0 and 255");
        }
        const std::string payload = arguments[1]->evaluate_string();
        this->enqueue_message(static_cast<uint8_t>(receiver), payload.c_str(), payload.size());
    } else if (method_name == "set_coordinator") {
        if (arguments.empty()) {
            throw std::runtime_error("set_coordinator expects at least one peer id");
        }
        std::vector<uint8_t> peers;
        peers.reserve(arguments.size());
        for (const auto &argument : arguments) {
            if ((argument->type & integer) == 0) {
                throw std::runtime_error("peer ids must be integers");
            }
            const long peer_value = argument->evaluate_integer();
            if (peer_value < 0 || peer_value > 255) {
                throw std::runtime_error("peer ids must be between 0 and 255");
            }
            peers.push_back(static_cast<uint8_t>(peer_value));
        }
        this->configure_coordinator(peers);
    } else if (method_name == "configure") {
        Module::expect(arguments, 2, integer, string);
        const int receiver = arguments[0]->evaluate_integer();
        if (receiver < 0 || receiver > 255) {
            throw std::runtime_error("receiver id must be between 0 and 255");
        }
        const uint8_t target = static_cast<uint8_t>(receiver);
        const std::string script = arguments[1]->evaluate_string();
        this->enqueue_message(target, "!-", 2);

        std::string current;
        auto flush_line = [&](void) {
            std::string trimmed = trim_copy(current);
            if (!trimmed.empty()) {
                std::string command = "!+" + trimmed;
                this->enqueue_message(target, command.c_str(), command.size());
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

void SerialBus::send_frame(uint8_t receiver, const char *payload, size_t length) const {
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

void SerialBus::send_done(uint8_t receiver) const {
    static constexpr char done_cmd[] = "__DONE__";
    this->send_frame(receiver, done_cmd, sizeof(done_cmd) - 1);
}

void SerialBus::send_response_line(uint8_t receiver, const char *line) {
    if (!line) {
        return;
    }
    char payload[PAYLOAD_CAPACITY];
    const int len = std::snprintf(payload, sizeof(payload), "%s:%s", RESPONSE_PREFIX, line);
    if (len < 0 || len >= static_cast<int>(sizeof(payload))) {
        echo("warning: serial bus %s response truncated", this->name.c_str());
        return;
    }
    try {
        this->enqueue_message(receiver, payload, len);
    } catch (const std::runtime_error &e) {
        echo("warning: serial bus %s could not send response: %s", this->name.c_str(), e.what());
    }
}

void SerialBus::echo_consumer_trampoline(const char *line, void *context) {
    auto *ctx = static_cast<ResponseContext *>(context);
    if (!ctx || !ctx->bus || !line) {
        return;
    }
    ctx->bus->send_response_line(ctx->receiver, line);
    ctx->sent_line = true;
}

void SerialBus::execute_remote_command(uint8_t requester, const char *payload, size_t length) {
    if (length == 0) {
        return;
    }
    ResponseContext context{this, requester, false};
    echo_push_consumer(echo_consumer_trampoline, &context);
    try {
        process_line(payload, length);
    } catch (const std::exception &e) {
        this->send_response_line(requester, e.what());
    } catch (...) {
        this->send_response_line(requester, "unknown error");
    }
    echo_pop_consumer(echo_consumer_trampoline, &context);
}

bool SerialBus::parse_frame(const char *line, BusFrame &frame) const {
    const std::string message(line);
    if (message.size() < 2 || message.compare(0, 2, "$$") != 0) {
        return false;
    }
    const size_t header_end = message.find("$$", 2);
    if (header_end == std::string::npos) {
        return false;
    }
    const std::string header = message.substr(2, header_end - 2);
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
        const size_t payload_len = message.size() - (header_end + 2);
        if (payload_len >= PAYLOAD_CAPACITY) {
            return false;
        }
        frame.sender = static_cast<uint8_t>(sender);
        frame.receiver = static_cast<uint8_t>(receiver);
        frame.length = payload_len;
        memcpy(frame.payload, message.c_str() + header_end + 2, payload_len);
        frame.payload[payload_len] = '\0';
    } catch (...) {
        return false;
    }
    return true;
}

bool SerialBus::handle_control_payload(const BusFrame &frame) {
    if (frame.length >= sizeof(RESPONSE_PREFIX) - 1 &&
        std::strncmp(frame.payload, RESPONSE_PREFIX, sizeof(RESPONSE_PREFIX) - 1) == 0) {
        const size_t prefix_len = sizeof(RESPONSE_PREFIX) - 1;
        const char *message = frame.payload + prefix_len;
        size_t message_len = frame.length > prefix_len ? frame.length - prefix_len : 0;
        if (message_len > 0 && message[0] == ':') {
            message++;
            message_len--;
        }
        if (message_len > 0) {
            static char buffer[PAYLOAD_CAPACITY];
            const size_t copy_len = std::min(message_len, static_cast<size_t>(sizeof(buffer) - 1));
            memcpy(buffer, message, copy_len);
            buffer[copy_len] = '\0';
            echo("bus[%u]: %s", frame.sender, buffer);
        }
        return true;
    }
    return false;
}

void SerialBus::handle_frame(const BusFrame &frame) {
    if (ota::bus_handle_frame(
            this->ota_session,
            frame.sender,
            std::string_view(frame.payload, frame.length),
            this->name.c_str(),
            [this](uint8_t receiver, const char *payload, size_t length) { this->enqueue_message(receiver, payload, length); })) {
        return;
    }
    if (this->handle_control_payload(frame)) {
        return;
    }
    if (frame.length > 0 && frame.payload[0] == '!') {
        process_line(frame.payload, frame.length);
        return;
    }
    this->execute_remote_command(frame.sender, frame.payload, frame.length);
}

void SerialBus::handle_poll_request(uint8_t sender) {
    if (this->is_coordinator()) {
        return;
    }
    this->window_requester = sender;
    this->transmit_window_open = true;
}

void SerialBus::handle_done(uint8_t sender) {
    if (!this->is_coordinator()) {
        return;
    }
    if (this->waiting_for_done && sender == this->current_poll_target) {
        this->waiting_for_done = false;
        this->current_poll_target = 0;
    }
}

void SerialBus::coordinator_poll_step() {
    if (this->peer_ids.empty()) {
        return;
    }
    this->current_poll_target = this->peer_ids[this->poll_index];
    this->poll_index = (this->poll_index + 1) % this->peer_ids.size();
    static constexpr char poll_cmd[] = "__POLL__";
    this->send_frame(this->current_poll_target, poll_cmd, sizeof(poll_cmd) - 1);
    this->waiting_for_done = true;
    this->poll_start_millis = millis();
}

void SerialBus::check_poll_timeout() {
    if (!this->waiting_for_done) {
        return;
    }
    if (millis_since(this->poll_start_millis) > POLL_TIMEOUT_MS) {
        echo("warning: serial bus %s poll to %u timed out", this->name.c_str(), this->current_poll_target);
        this->waiting_for_done = false;
        this->current_poll_target = 0;
    }
}
