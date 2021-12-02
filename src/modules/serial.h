#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"
#include "module.h"

class Serial : public Module
{
private:
    const uart_port_t uart_num;

public:
    Serial(const std::string name,
           const gpio_num_t rx_pin, const gpio_num_t tx_pin, const long baud_rate, const uart_port_t uart_num);

    int available() const;
    int read(const uint32_t timeout = 0) const;
    size_t write(const uint8_t byte) const;
    void flush() const;
    void clear() const;
};
