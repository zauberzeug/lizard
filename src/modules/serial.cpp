#include "serial.h"

#define RX_BUF_SIZE 1024
#define TX_BUF_SIZE 1024

Serial::Serial(const std::string name,
               const gpio_num_t rx_pin, const gpio_num_t tx_pin, const long baud_rate, const uart_port_t uart_num)
    : Module(serial, name), uart_num(uart_num)
{
    if (uart_is_driver_installed(uart_num))
    {
        throw std::runtime_error("serial interface is already in use");
    }

    const uart_config_t uart_config = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .use_ref_tick = false,
    };
    uart_param_config(uart_num, &uart_config);
    uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(uart_num, RX_BUF_SIZE, TX_BUF_SIZE, 0, NULL, 0);
}

size_t Serial::write(const uint8_t byte) const
{
    const char send = byte;
    uart_write_bytes(this->uart_num, &send, 1);
    return 1;
}

int Serial::available() const
{
    size_t available;
    uart_get_buffered_data_len(this->uart_num, &available);
    return available;
}

void Serial::flush() const
{
    uart_flush(this->uart_num);
}

int Serial::read(uint32_t timeout) const
{
    uint8_t data = 0;
    const int length = uart_read_bytes(this->uart_num, &data, 1, timeout);
    return length > 0 ? data : -1;
}

void Serial::clear() const
{
    while (this->available())
    {
        this->read();
    }
}
