#include "serial.h"
#include "utils/string_utils.h"
#include "utils/timing.h"
#include "utils/uart.h"
#include "utils/uart_driver.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

#define RX_BUF_SIZE (2 * CONSOLE_LINE_SIZE) // a maximal line plus what arrives while the main loop handles it
#define TX_BUF_SIZE 2048
#define UART_PATTERN_QUEUE_SIZE 100

// A UART number and its pins belong to a Serial for its whole lifetime, also after deinstall(),
// so that a later Serial cannot be torn down by the earlier one's next deinstall().
static const Serial *uart_owners[UART_NUM_MAX] = {};
static const Serial *pin_owners[GPIO_NUM_MAX] = {};

static Module_ptr create_serial(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler) {
    Module::expect(arguments, 4, integer, integer, integer, integer);
    const gpio_num_t rx_pin = (gpio_num_t)arguments[0]->evaluate_integer();
    const gpio_num_t tx_pin = (gpio_num_t)arguments[1]->evaluate_integer();
    const long baud_rate = arguments[2]->evaluate_integer();
    const uart_port_t uart_num = (uart_port_t)arguments[3]->evaluate_integer();
    return std::make_shared<Serial>(name, rx_pin, tx_pin, baud_rate, uart_num);
}
REGISTER_MODULE(Serial, &create_serial)

const std::map<std::string, Variable_ptr> Serial::get_defaults() {
    return {};
}

Serial::Serial(const std::string name,
               const gpio_num_t rx_pin, const gpio_num_t tx_pin, const long baud_rate, const uart_port_t uart_num)
    : Module(name), rx_pin(rx_pin), tx_pin(tx_pin), baud_rate(baud_rate), uart_num(uart_num) {
    this->properties = Serial::get_defaults();

    if (uart_num < 0 || uart_num >= UART_NUM_MAX) {
        throw std::runtime_error("invalid uart number");
    }
    if (rx_pin < 0 || rx_pin >= GPIO_NUM_MAX || tx_pin < 0 || tx_pin >= GPIO_NUM_MAX) {
        throw std::runtime_error("invalid pin");
    }
    if (uart_owners[uart_num]) {
        throw std::runtime_error("uart " + std::to_string(uart_num) +
                                 " is reserved by serial \"" + uart_owners[uart_num]->name + "\"");
    }
    if (uart_is_driver_installed(uart_num)) {
        throw std::runtime_error("serial interface is already in use");
    }
    for (const gpio_num_t pin : {rx_pin, tx_pin}) {
        if (pin_owners[pin]) {
            throw std::runtime_error("pin " + std::to_string(pin) +
                                     " is reserved by serial \"" + pin_owners[pin]->name + "\"");
        }
    }

    this->initialize_uart();
    uart_owners[uart_num] = this;
    pin_owners[rx_pin] = this;
    pin_owners[tx_pin] = this;
}

Serial::~Serial() {
    this->deinstall();
    uart_owners[this->uart_num] = nullptr;
    pin_owners[this->rx_pin] = nullptr;
    pin_owners[this->tx_pin] = nullptr;
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
    if (install_uart_driver_on_core1(uart_num, RX_BUF_SIZE, TX_BUF_SIZE) != ESP_OK) {
        throw std::runtime_error("could not install the uart driver");
    }
}

void Serial::enable_line_detection() const {
    uart_enable_pattern_det_baud_intr(this->uart_num, '\n', 1, 9, 0, 0);
    uart_pattern_queue_reset(this->uart_num, UART_PATTERN_QUEUE_SIZE);
}

void Serial::claim(const std::string &user) const {
    this->users.push_back(user);
}

void Serial::require_sole_user(const std::string &user) const {
    for (const std::string &other : this->users) {
        if (other != user) {
            throw std::runtime_error("serial \"" + this->name + "\" is in use by \"" + other + "\"");
        }
    }
}

void Serial::deinstall() const {
    this->pending_lines.clear();
    this->discarding = false;
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
            uart_write_bytes(this->uart_num, checksum_buffer, CHECKSUM_TRAILER_LENGTH);
            start = i + 1;
            checksum = 0;
        } else {
            checksum ^= message[i];
        }
    }
}

int Serial::available() const {
    size_t available = 0;
    uart_get_buffered_data_len(this->uart_num, &available);
    return available;
}

bool Serial::has_buffered_lines() const {
    if (!this->pending_lines.empty()) {
        return true;
    }
    while (true) {
        const int pos = uart_pattern_get_pos(this->uart_num);
        if (pos >= 0) {
            if (!this->discarding) {
                return true;
            }
            // the rest of an overlong run, up to its line end
            uart_pattern_pop_pos(this->uart_num);
            this->discard(pos + 1);
            this->discarding = false;
            continue;
        }
        size_t buffered = 0;
        uart_get_buffered_data_len(this->uart_num, &buffered);
        if (buffered == 0) {
            return false;
        }
        if (uart_pattern_get_pos(this->uart_num) >= 0) {
            continue; // a line end arrived after the first check, so `buffered` may contain complete lines
        }
        if (this->discarding) {
            this->discard(buffered);
            return false;
        }
        return buffered > CONSOLE_LINE_SIZE; // read_line() drops it and reports LINE_UNTERMINATED
    }
}

void Serial::flush() const {
    this->pending_lines.clear();
    this->discarding = false;
    uart_flush(this->uart_num);
}

// drops `count` bytes from the receive ring, reading them in chunks
void Serial::discard(int count) const {
    uint8_t scratch[128];
    while (count > 0) {
        const int read = uart_read_bytes(this->uart_num, scratch, std::min<int>(count, sizeof(scratch)), 0);
        if (read <= 0) {
            break;
        }
        count -= read;
    }
}

int Serial::read(uint32_t timeout) const {
    uint8_t data = 0;
    const int length = uart_read_bytes(this->uart_num, &data, 1, timeout);
    return length > 0 ? data : -1;
}

int Serial::read_line(char *buffer, size_t buffer_len) const {
    if (!this->pending_lines.empty()) {
        const size_t line_end = this->pending_lines.find('\n');
        const size_t len = line_end == std::string::npos ? this->pending_lines.size() : line_end + 1;
        if (len > buffer_len) {
            this->pending_lines.erase(0, len);
            return LINE_DISCARDED;
        }
        this->pending_lines.copy(buffer, len);
        this->pending_lines.erase(0, len);
        return len;
    }
    const int pos = uart_pattern_pop_pos(this->uart_num);
    if (pos < 0) {
        size_t buffered = 0;
        uart_get_buffered_data_len(this->uart_num, &buffered);
        if (buffered > CONSOLE_LINE_SIZE && uart_pattern_get_pos(this->uart_num) < 0) {
            // bytes without a line end that already exceed a line can never become one; a ring they fill up
            // disables the receive interrupts until something reads or flushes, so drop them now
            this->discard(buffered);
            this->discarding = true;
            return LINE_UNTERMINATED;
        }
        return 0;
    }
    if (pos >= static_cast<int>(buffer_len)) {
        if (this->available() <= pos) {
            uart_flush_input(this->uart_num);
            while (uart_pattern_pop_pos(this->uart_num) > 0)
                ;
            return LINE_FLUSHED;
        }
        this->discard(pos + 1);
        return LINE_DISCARDED;
    }
    const int len = uart_read_bytes(this->uart_num, (uint8_t *)buffer, pos + 1, 0);
    // the driver queues only the last line end of each receive chunk, so one read can hold several lines
    const char *const line_end = len > 0 ? static_cast<const char *>(memchr(buffer, '\n', len)) : nullptr;
    const int first_len = line_end ? line_end - buffer + 1 : len;
    if (first_len < len) {
        this->pending_lines.assign(buffer + first_len, len - first_len);
    }
    return first_len;
}

const char *Serial::read_line_error(const int result) {
    return result == LINE_FLUSHED        ? "buffer too small, but cannot discard line. flushed serial."
           : result == LINE_UNTERMINATED ? "input exceeds a line without a line end. discarded up to the next line end."
                                         : "buffer too small. discarded line.";
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
