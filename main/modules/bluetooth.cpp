#include "bluetooth.h"
#include "../main.h"
#include "../storage.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "uart.h"
#include <atomic>
#include <cstring>
#include <memory>
#include <stdexcept>

static Module_ptr create_bluetooth(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler message_handler) {
    Module::expect(arguments, 1, string);
    const std::string device_name = arguments[0]->evaluate_string();
    return std::make_shared<Bluetooth>(name, device_name, message_handler);
}
REGISTER_MODULE(Bluetooth, &create_bluetooth)

const std::map<std::string, Variable_ptr> Bluetooth::get_defaults() {
    return {
        {"connected", std::make_shared<BooleanVariable>(false)},
        {"last_message_age", std::make_shared<IntegerVariable>(0)},
    };
}

static constexpr size_t LINE_QUEUE_LENGTH = 32;
static constexpr size_t CONSOLE_QUEUE_LENGTH = 64;
static constexpr size_t OUTPUT_QUEUE_LENGTH = 64;
static constexpr size_t CONSOLE_QUEUE_BYTES = 8192; // both console directions, so that a flood cannot exhaust the heap
static std::atomic<uint32_t> dropped_lines{0};
static std::atomic<uint32_t> dropped_output_lines{0};

Bluetooth::Bluetooth(const std::string name, const std::string device_name, MessageHandler message_handler)
    : Module(name), device_name(device_name), message_handler(message_handler) {
    if (!ZZ::BleCommand::claim_host(TYPE)) {
        throw std::runtime_error("the BLE radio is already used by a BleBridge");
    }
    if (!(this->line_queue = xQueueCreate(LINE_QUEUE_LENGTH, sizeof(char *)))) {
        throw std::runtime_error("failed to create bluetooth line queue");
    }
    this->console_queue = std::make_unique<LineQueue>(CONSOLE_QUEUE_LENGTH, CONSOLE_QUEUE_BYTES);
    ZZ::BleCommand::set_console_callback(
        [queue = this->console_queue.get()](const char *line, size_t len) { return queue->push_copy(line, len); });
    // NOTE: This callback runs on the NimBLE host task, whose stack is far too small for the parser.
    // It only queues the line (without blocking or echoing); step() parses it on the main task.
    ZZ::BleCommand::init(device_name, [queue = this->line_queue](std::unique_ptr<char[]> line) {
        char *raw = line.get();
        if (xQueueSend(queue, &raw, 0) == pdTRUE) {
            line.release();
        } else {
            dropped_lines.fetch_add(1, std::memory_order_relaxed);
        }
    });
    // mirrors every console line to a listening bridge, with its checksum like on UART0
    register_echo_callback([this](const char *line) {
        BleLineStream *const stream = this->console_output.load();
        if (stream == nullptr || !ZZ::BleCommand::console_ready()) {
            return;
        }
        uint8_t checksum = 0;
        const size_t len = strlen(line);
        for (size_t i = 0; i < len; ++i) {
            checksum ^= line[i];
        }
        char end[5];
        snprintf(end, sizeof(end), "@%02x\n", checksum);
        if (!stream->push(line, len, end)) {
            dropped_output_lines.fetch_add(1, std::memory_order_relaxed);
        }
    });
    this->properties = Bluetooth::get_defaults();
}

void Bluetooth::step() {
    if (const uint32_t dropped = dropped_lines.exchange(0, std::memory_order_relaxed)) {
        echo("warning: dropped %lu bluetooth lines because the line queue was full", static_cast<unsigned long>(dropped));
    }
    char *raw;
    while (xQueueReceive(this->line_queue, &raw, 0) == pdTRUE) {
        const std::unique_ptr<char[]> line(raw);
        this->last_message_millis = millis();
        try {
            this->message_handler(line.get(), true, false);
        } catch (const std::exception &e) {
            echo("error in bluetooth message handler: %s", e.what());
        }
    }
    if (this->console_output.load() == nullptr && !this->console_failed && ZZ::BleCommand::console_ready()) {
        // the send task only exists once a bridge listens, so robots without one keep the heap
        try {
            this->console_output = new BleLineStream("ble_console", OUTPUT_QUEUE_LENGTH, CONSOLE_QUEUE_BYTES, ZZ::BleCommand::console_ready,
                                                     ZZ::BleCommand::console_chunk_size, ZZ::BleCommand::send_console);
        } catch (const std::exception &e) {
            this->console_failed = true;
            echo("error: the bluetooth console is not available: %s", e.what());
        }
    }
    if (const uint32_t dropped = ZZ::BleCommand::take_dropped_console_lines()) {
        echo("warning: dropped %lu bluetooth console lines because the line queue was full", static_cast<unsigned long>(dropped));
    }
    size_t raw_len;
    while (const std::unique_ptr<char[]> line = this->console_queue->pop(0, &raw_len)) {
        this->last_message_millis = millis();
        ZZ::BleCommand::release_console_line(raw_len);
        bool checksum_ok = true;
        const int len = check(line.get(), strlen(line.get()), &checksum_ok);
        if (!checksum_ok) {
            echo("warning: Checksum mismatch while processing bluetooth console");
            continue;
        }
        try {
            process_line(line.get(), len);
        } catch (const std::exception &e) {
            echo("error processing bluetooth console: %s", e.what());
        }
    }
    ZZ::BleCommand::send_console_flow(); // lets a flow-controlled sender refill the queue
    // a console that outputs more than the link carries drops lines in every step, so this is reported once a second
    this->dropped_output += dropped_output_lines.exchange(0, std::memory_order_relaxed);
    if (this->dropped_output > 0 && millis_since(this->last_drop_warning_millis) >= 1000) {
        echo("warning: dropped %lu console lines for the bluetooth console", static_cast<unsigned long>(this->dropped_output));
        this->dropped_output = 0;
        this->last_drop_warning_millis = millis();
    }
    this->properties.at("connected")->set_boolean_value(ZZ::BleCommand::is_connected());
    this->properties.at("last_message_age")->set_integer_value(millis_since(this->last_message_millis));
    Module::step();
}

void Bluetooth::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "send") {
        expect(arguments, 1, string);
        ZZ::BleCommand::send(arguments[0]->evaluate_string());
    } else if (method_name == "set_pin") {
        expect(arguments, 1, integer);
        const int64_t pin = arguments[0]->evaluate_integer();
        if (pin < 0 || pin > 999999) {
            throw std::runtime_error("PIN must be a 6-digit non-negative integer (000000-999999)");
        }
        Storage::set_user_pin(static_cast<std::uint32_t>(pin));
        echo("User PIN set successfully");
    } else if (method_name == "get_pin") {
        expect(arguments, 0);
        std::uint32_t pin;
        if (Storage::get_user_pin(pin)) {
            echo("%06u", static_cast<unsigned>(pin));
        } else {
            echo("No user PIN set");
        }
    } else if (method_name == "reset_pin") {
        expect(arguments, 0);
        Storage::remove_user_pin();
        echo("User PIN has been reset.");
    } else if (method_name == "reset_bonds") {
        expect(arguments, 0);
        ZZ::BleCommand::reset_bonds();
        echo("Bluetooth bonds reset and BLE restarted. All peers must re-pair.");
    } else if (method_name == "deactivate_pin") {
        expect(arguments, 0);
        ZZ::BleCommand::deactivate_pin();
        echo("Bluetooth PIN/security deactivated - connections are unauthenticated");
    } else {
        Module::call(method_name, arguments);
    }
}
