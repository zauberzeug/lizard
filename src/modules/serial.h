#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"
#include "module.h"

class Serial : public Module
{
private:
    uart_port_t uart_num;

public:
    Serial(std::string name, gpio_num_t rx_pin, gpio_num_t tx_pin, long baud_rate, uart_port_t uart_num);

    int available();
    int read(uint32_t timeout = 0);
    size_t write(uint8_t byte);
    void flush();
    void clear();
};
