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

// Reserves a resource for good, e.g. the console pins at boot.
void reserve(const Kind kind, const int index, const std::string &owner);

// Reserves the console UART with its pins and the pins of the SPI flash, which no module may use.
void reserve_boot_resources();

// The claims of one module; they are released together when the module is destroyed.
class Claims {
public:
    explicit Claims(const std::string &owner);
    ~Claims();
    Claims(const Claims &) = delete;
    Claims &operator=(const Claims &) = delete;

    void pin(const gpio_num_t pin);
    void uart(const uart_port_t port);
    void can();                  // the single TWAI controller
    void adc(const int unit_id); // 1 or 2, as the AnalogUnit argument counts

private:
    const std::string owner;
    std::vector<std::pair<Kind, int>> claimed;
    void claim(const Kind kind, const int index);
};

} // namespace resources
