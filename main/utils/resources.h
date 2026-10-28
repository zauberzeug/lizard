#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"
#include <string>
#include <utility>
#include <vector>

// Pins and peripherals belong to one owner at a time, so that a second module on the same pin fails with a message
// naming the first instead of silently reconfiguring it.
namespace resources {

enum class Kind { pin,
                  uart,
                  can,
                  adc };

// Reserves the console UART with its pins and the pins of the SPI flash, which no module may use.
void reserve_boot_resources();

// The claims of one owner; they are released together when the owner is destroyed.
class Claims {
public:
    // `name` is kept by reference, so it must outlive the claims; a module owner is shown as `module "<name>"`
    explicit Claims(const std::string &name, const bool is_module = true);
    ~Claims();
    Claims(const Claims &) = delete;
    Claims &operator=(const Claims &) = delete;

    void pin(const gpio_num_t pin);
    void uart(const uart_port_t port);
    void can();                  // the single TWAI controller
    void adc(const int unit_id); // 1 or 2, as the AnalogUnit argument counts
    std::string owner() const;

private:
    const std::string &name;
    const bool is_module;
    std::vector<std::pair<Kind, int>> claimed;
    void claim(const Kind kind, const int index);
};

} // namespace resources
