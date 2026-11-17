#include "bluetooth.h"
#include "../storage.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "uart.h"
#include <atomic>
#include <cmath>
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
    ZZ::BleCommand::ScanResult result;
    if (ZZ::BleCommand::take_scan_result(result)) {
        if (result.outcome == ZZ::BleCommand::ScanOutcome::found) {
            echo("BLE scan: found \"%s\" %02x:%02x:%02x:%02x:%02x:%02x rssi=%d", result.name,
                 result.address[0], result.address[1], result.address[2],
                 result.address[3], result.address[4], result.address[5], result.rssi);
        } else if (result.outcome == ZZ::BleCommand::ScanOutcome::aborted) {
            echo("BLE scan: \"%s\" aborted", result.name);
        } else {
            echo("BLE scan: \"%s\" not found", result.name);
        }
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
    } else if (method_name == "deactivate_pin") {
        expect(arguments, 0);
        ZZ::BleCommand::deactivate_pin();
        echo("Bluetooth PIN/security deactivated - connections are unauthenticated");
    } else if (method_name == "scan") {
        this->scan(arguments);
    } else {
        Module::call(method_name, arguments);
    }
}

void Bluetooth::scan(const std::vector<ConstExpression_ptr> &arguments) {
    if (arguments.size() < 1 || arguments.size() > 2) {
        throw std::runtime_error("unexpected number of arguments");
    }
    expect(arguments, -1, string, numbery);
    const std::string name = arguments[0]->evaluate_string();
    if (name.empty()) {
        throw std::runtime_error("name must not be empty");
    }
    const double timeout = arguments.size() > 1 ? arguments[1]->evaluate_number() : 5.0;
    if (!(timeout >= 0.001 && timeout <= 60)) { // written this way to refuse NaN as well
        throw std::runtime_error("timeout must be between 0.001 and 60 seconds");
    }
    // Rounded, because 0 ms would make NimBLE scan for its default of 10.24 s
    if (const char *error = ZZ::BleCommand::scan(name, static_cast<uint32_t>(std::lround(timeout * 1000)))) {
        throw std::runtime_error(error);
    }
}
