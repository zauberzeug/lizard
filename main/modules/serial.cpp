#include "serial.h"
#include "utils/string_utils.h"
#include "utils/timing.h"
#include "utils/uart.h"
#include <cstring>
#include <stdexcept>

#define RX_BUF_SIZE 2048
#define TX_BUF_SIZE 2048
#define UART_PATTERN_QUEUE_SIZE 100

const std::map<std::string, Variable_ptr> Serial::get_defaults() {
    return {};
}

Serial::Serial(const std::string name,
               const gpio_num_t rx_pin, const gpio_num_t tx_pin, const long baud_rate, const uart_port_t uart_num)
    : Module(serial, name), rx_pin(rx_pin), tx_pin(tx_pin), baud_rate(baud_rate), uart_num(uart_num) {
    this->merge_properties(Serial::get_defaults());

    if (uart_is_driver_installed(uart_num)) {
        throw std::runtime_error("serial interface is already in use");
    }

    this->initialize_uart();
}

void Serial::initialize_uart() const {
    const uart_config_t uart_config = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
    };
    uart_param_config(uart_num, &uart_config);
    uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(uart_num, RX_BUF_SIZE, TX_BUF_SIZE, UART_PATTERN_QUEUE_SIZE, NULL, 0);
}

void Serial::enable_line_detection() const {
    uart_enable_pattern_det_baud_intr(this->uart_num, '\n', 1, 9, 0, 0);
    uart_pattern_queue_reset(this->uart_num, UART_PATTERN_QUEUE_SIZE);
}

void Serial::deinstall() const {
    if (uart_is_driver_installed(this->uart_num)) {
        uart_driver_delete(this->uart_num);
    }
    gpio_reset_pin(this->rx_pin);
    gpio_reset_pin(this->tx_pin);
    gpio_set_direction(this->rx_pin, GPIO_MODE_INPUT);
    gpio_set_direction(this->tx_pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(this->rx_pin, GPIO_FLOATING);
    gpio_set_pull_mode(this->tx_pin, GPIO_FLOATING);
}

void Serial::reinitialize_after_flash() const {
    this->deinstall();
    delay(50);
    this->initialize_uart();
    this->enable_line_detection();
}

size_t Serial::write(const uint8_t byte) const {
    const char send = byte;
    uart_write_bytes(this->uart_num, &send, 1);
    return 1;
}

void Serial::write_checked_line(const char *message) const {
    this->write_checked_line(message, std::strlen(message));
}

void Serial::write_checked_line(const char *message, const int length) const {
    static char checksum_buffer[16];
    uint8_t checksum = 0;
    int start = 0;
    for (unsigned int i = 0; i < length + 1; ++i) {
        if (i >= length || message[i] == '\n') {
            csprintf(checksum_buffer, sizeof(checksum_buffer), "@%02x\n", checksum);
            uart_write_bytes(this->uart_num, &message[start], i - start);
            uart_write_bytes(this->uart_num, checksum_buffer, 4);
            start = i + 1;
            checksum = 0;
        } else {
            checksum ^= message[i];
        }
    }
}

int Serial::available() const {
    if (!uart_is_driver_installed(this->uart_num)) {
        return 0;
    }
    size_t available;
    uart_get_buffered_data_len(this->uart_num, &available);
    return available;
}

bool Serial::has_buffered_lines() const {
    return uart_pattern_get_pos(this->uart_num) != -1;
}

void Serial::flush() const {
    uart_flush(this->uart_num);
}

int Serial::read(uint32_t timeout) const {
    uint8_t data = 0;
    const int length = uart_read_bytes(this->uart_num, &data, 1, timeout);
    return length > 0 ? data : -1;
}

int Serial::read_line(char *buffer, size_t buffer_len) const {
    int pos = uart_pattern_pop_pos(this->uart_num);
    if (pos >= static_cast<int>(buffer_len)) {
        if (this->available() < pos) {
            uart_flush_input(this->uart_num);
            while (uart_pattern_pop_pos(this->uart_num) > 0)
                ;
            throw std::runtime_error("buffer too small, but cannot discard line. flushed serial.");
        }

        for (int i = 0; i < pos; i++)
            this->read();
        throw std::runtime_error("buffer too small. discarded line.");
    }
    return pos >= 0 ? uart_read_bytes(this->uart_num, (uint8_t *)buffer, pos + 1, 0) : 0;
}

void Serial::clear() const {
    while (this->available()) {
        this->read();
    }
}

std::string Serial::get_output() const {
    if (!this->available()) {
        return "";
    }

    static char buffer[256];
    int byte;
    int pos = 0;
    while ((byte = this->read()) >= 0) {
        pos += csprintf(&buffer[pos], sizeof(buffer) - pos, pos == 0 ? "%02x" : " %02x", byte);
    }
    return buffer;
}

void Serial::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "send") {
        for (auto const &argument : arguments) {
            if ((argument->type & integer) == 0) {
                throw std::runtime_error("type mismatch at argument");
            }
            this->write(argument->evaluate_integer());
        }
    } else if (method_name == "read") {
        const std::string output = this->get_output();
        echo("%s %s", this->name.c_str(), output.c_str());
    } else {
        Module::call(method_name, arguments);
    }
}
