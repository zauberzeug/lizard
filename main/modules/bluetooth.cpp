#include "bluetooth.h"
#include "../storage.h"
#include "../utils/uart.h"
#include "uart.h"
#include <atomic>
#include <memory>
#include <stdexcept>

static Module_ptr create_bluetooth(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler message_handler) {
    Module::expect(arguments, 1, string);
    const std::string device_name = arguments[0]->evaluate_string();
    return std::make_shared<Bluetooth>(name, device_name, message_handler);
}
REGISTER_MODULE(Bluetooth, &create_bluetooth)

const std::map<std::string, Variable_ptr> Bluetooth::get_defaults() {
    return {};
}

static constexpr size_t LINE_QUEUE_LENGTH = 32;
static std::atomic<uint32_t> dropped_lines{0};

Bluetooth::Bluetooth(const std::string name, const std::string device_name, MessageHandler message_handler)
    : Module(name), device_name(device_name), message_handler(message_handler) {
    if (!(this->line_queue = xQueueCreate(LINE_QUEUE_LENGTH, sizeof(char *)))) {
        throw std::runtime_error("failed to create bluetooth line queue");
    }
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
    this->properties = Bluetooth::get_defaults();
}

void Bluetooth::step() {
    if (const uint32_t dropped = dropped_lines.exchange(0, std::memory_order_relaxed)) {
        echo("warning: dropped %lu bluetooth lines because the line queue was full", static_cast<unsigned long>(dropped));
    }
    char *raw;
    while (xQueueReceive(this->line_queue, &raw, 0) == pdTRUE) {
        const std::unique_ptr<char[]> line(raw);
        try {
            this->message_handler(line.get(), true, false);
        } catch (const std::exception &e) {
            echo("error in bluetooth message handler: %s", e.what());
        }
    }
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
