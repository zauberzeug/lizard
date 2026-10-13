#include "serial.h"
#include "utils/string_utils.h"
#include "utils/timing.h"
#include "utils/uart.h"
#include "utils/uart_driver.h"
#include <cstring>
#include <stdexcept>

#define RX_BUF_SIZE (2 * CONSOLE_LINE_SIZE) // a maximal line plus what arrives while the main loop handles it
#define TX_BUF_SIZE 2048
#define UART_PATTERN_QUEUE_SIZE 100

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

    // the claims outlive deinstall(), so a later Serial cannot be torn down by this one's next deinstall()
    this->claims.uart(uart_num);
    this->claims.pin(rx_pin);
    this->claims.pin(tx_pin);
    if (uart_is_driver_installed(uart_num)) {
        throw std::runtime_error("serial interface is already in use");
    }

    this->initialize_uart();
}

Serial::~Serial() {
    this->deinstall();
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
    this->apply_rx_full_threshold();
}

void Serial::apply_rx_full_threshold() const {
    // An unmuted Serial prints the ring as one line, so a frame up to the threshold must arrive in one piece.
    uart_set_rx_full_threshold(this->uart_num,
                               this->output_on ? RX_FULL_THRESHOLD_DEFAULT_BYTES : RX_FULL_THRESHOLD_BYTES);
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
    if (!this->pending_lines.empty() || uart_pattern_get_pos(this->uart_num) != -1) {
        return true;
    }
    // a ring that fills without a line end stops receiving, so drop what can never become a line; the driver
    // updates the byte count and the pattern queue together, so without a pattern all `buffered` bytes are unterminated
    const int buffered = this->available();
    if (buffered > CONSOLE_LINE_SIZE && uart_pattern_get_pos(this->uart_num) == -1) {
        discard_uart_input(this->uart_num, buffered);
    }
    return false;
}

void Serial::flush() const {
    this->pending_lines.clear();
    uart_flush(this->uart_num);
}

int Serial::read(uint32_t timeout) const {
    uint8_t data = 0;
    const int length = uart_read_bytes(this->uart_num, &data, 1, timeout);
    return length > 0 ? data : -1;
}

int Serial::read_line(char *buffer, size_t buffer_len) const {
    if (!this->pending_lines.empty()) {
        const size_t end = this->pending_lines.find('\n');
        const size_t len = end == std::string::npos ? this->pending_lines.size() : end + 1;
        if (len > buffer_len) {
            this->pending_lines.erase(0, len);
            return LINE_DISCARDED;
        }
        this->pending_lines.copy(buffer, len);
        this->pending_lines.erase(0, len);
        return len;
    }
    int pos = uart_pattern_pop_pos(this->uart_num);
    if (pos >= static_cast<int>(buffer_len)) {
        // also when the line end sits in the block the driver parked beside a full ring: reading makes room for it
        discard_uart_input(this->uart_num, pos + 1);
        return LINE_DISCARDED;
    }
    if (pos < 0) {
        return 0;
    }
    const int len = uart_read_bytes(this->uart_num, (uint8_t *)buffer, pos + 1, 0);
    // the driver queues only the last line end of each receive chunk, so one read can hold several lines
    const char *line_end = len > 0 ? static_cast<const char *>(memchr(buffer, '\n', len)) : nullptr;
    const int first_len = line_end ? line_end - buffer + 1 : len;
    if (first_len < len) {
        this->pending_lines.assign(buffer + first_len, len - first_len);
    }
    return first_len;
}

const char *Serial::read_line_error(const int result) {
    return "buffer too small. discarded line.";
}

std::string Serial::get_output() const {
    if (!this->available()) {
        return "";
    }

    static char buffer[256];
    int byte;
    int pos = 0;
    // stop once " xx" and the terminator no longer fit; the rest stays in the ring for the next line
    while (pos + 4 <= static_cast<int>(sizeof(buffer)) && (byte = this->read()) >= 0) {
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
    } else if (method_name == "mute" || method_name == "unmute") {
        Module::call(method_name, arguments);
        this->apply_rx_full_threshold();
    } else {
        Module::call(method_name, arguments);
    }
}
