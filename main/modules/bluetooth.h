#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "module.h"
#include "utils/ble_command.h"
#include "utils/ble_line_stream.h"
#include "utils/line_queue.h"
#include <atomic>
#include <memory>
#include <string>

class Bluetooth;
using Bluetooth_ptr = std::shared_ptr<Bluetooth>;
using ConstBluetooth_ptr = std::shared_ptr<const Bluetooth>;

class Bluetooth : public Module {
private:
    const std::string device_name;
    const MessageHandler message_handler;
    QueueHandle_t line_queue;
    std::unique_ptr<LineQueue> console_queue;             // console lines from a BLE bridge, run like UART0 lines
    std::atomic<BleLineStream *> console_output{nullptr}; // every console line for the bridge, created when one listens
    bool console_failed = false;                          // creating it failed, e.g. for lack of heap
    uint32_t dropped_output = 0;                          // output lines the bridge could not take, not reported yet
    unsigned long last_drop_warning_millis = 0;
    unsigned long last_message_millis = 0; // `millis()` when the last received line was handed to the interpreter

public:
    static inline constexpr const char *TYPE = "Bluetooth";

    Bluetooth(const std::string name, const std::string device_name, MessageHandler message_handler);

    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
