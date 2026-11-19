#pragma once

#include "console_bridge.h"
#include "utils/ble_line_stream.h"
#include <cstdint>
#include <memory>
#include <string>

// Dongle side of the Bluetooth console: a BLE central linked to a robot's Bluetooth module (see module reference)
class BleBridge;
using BleBridge_ptr = std::shared_ptr<BleBridge>;

class BleBridge : public ConsoleBridge {
public:
    static inline constexpr const char *TYPE = "BleBridge";
    static constexpr size_t MAX_DEVICE_NAME = 29;

    BleBridge(const std::string name);

    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

protected:
    void forward(const char *line, size_t len) override;

private:
    std::unique_ptr<BleLineStream> console_input; // UART0 lines for the robot
    bool was_linked = false;
    std::string linked_name; // robot of the last established link, for the messages
    int64_t rx = 0;
    int64_t tx = 0;
    int64_t lost = 0;
    int64_t lost_reported = 0;
    unsigned long last_lost_warning_ms = 0;
};
