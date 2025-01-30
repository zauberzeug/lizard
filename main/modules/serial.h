#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"
#include "module.h"
#include <memory>
#include <string>

class Serial;
using Serial_ptr = std::shared_ptr<Serial>;
using ConstSerial_ptr = std::shared_ptr<const Serial>;

class Serial : public Module {

protected:
    void set_error_descriptions() override;

public:
    const gpio_num_t rx_pin;
    const gpio_num_t tx_pin;
    const long baud_rate;
    const uart_port_t uart_num;

    Serial(const std::string name,
           const gpio_num_t rx_pin, const gpio_num_t tx_pin, const long baud_rate, const uart_port_t uart_num);
    void initialize_uart() const;
    void enable_line_detection() const;
    void deinstall() const;
    void reinitialize_after_flash() const;
    int available() const;
    bool has_buffered_lines() const;
    int read(const uint32_t timeout = 0) const;
    int read_line(char *buffer, size_t buffer_len) const;
    size_t write(const uint8_t byte) const;
    void write_checked_line(const char *message) const;
    void write_checked_line(const char *message, const int length) const;
    void flush() const;
    void clear() const;
    std::string get_output() const override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
