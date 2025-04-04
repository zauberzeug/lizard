#include "serial.h"
#include "utils/string_utils.h"
#include "utils/timing.h"
#include "utils/uart.h"
#include <cstring>
#include <stdexcept>

#define RX_BUF_SIZE 2048
#define TX_BUF_SIZE 2048
#define UART_PATTERN_QUEUE_SIZE 100

REGISTER_MODULE_DEFAULTS(Serial)

const std::map<std::string, Variable_ptr> Serial::get_defaults() {
    return {};
}

Serial::Serial(const std::string name,
               const gpio_num_t rx_pin, const gpio_num_t tx_pin, const long baud_rate, const uart_port_t uart_num)
    : Module(serial, name), rx_pin(rx_pin), tx_pin(tx_pin), baud_rate(baud_rate), uart_num(uart_num) {
    this->properties = Serial::get_defaults();

    if (uart_is_driver_installed(uart_num)) {
        throw std::runtime_error("serial interface is already in use");
    }

    this->initialize_uart();

    // Create receive task
    xTaskCreate(rx_task,
                ("serial_rx_" + name).c_str(),
                4096, // Stack size
                this, // Task parameter
                5,    // Priority
                &rx_task_handle);

    // Create transmit task
    xTaskCreate(tx_task,
                ("serial_tx_" + name).c_str(),
                4096, // Stack size
                this, // Task parameter
                5,    // Priority
                &tx_task_handle);
}

Serial::~Serial() {
    if (rx_task_handle != nullptr) {
        vTaskDelete(rx_task_handle);
        rx_task_handle = nullptr;
    }
    if (tx_task_handle != nullptr) {
        vTaskDelete(tx_task_handle);
        tx_task_handle = nullptr;
    }
    deinstall();
}

void Serial::rx_task(void *param) {
    Serial *serial = static_cast<Serial *>(param);
    serial->rx_task_function();
}

void Serial::tx_task(void *param) {
    Serial *serial = static_cast<Serial *>(param);
    serial->tx_task_function();
}

void Serial::rx_task_function() {
    uint8_t data;
    while (true) {
        size_t len = uart_read_bytes(uart_num, &data, 1, portMAX_DELAY);
        if (len == 1) {
            rx_buffer.write(data);
        }
    }
}

void Serial::tx_task_function() {
    static uint8_t tx_data[128]; // Buffer for reading chunks of data
    while (true) {
        if (tx_buffer.available() > 0) {
            size_t chunk_size = 0;
            // Read up to 128 bytes at a time from tx_buffer
            while (chunk_size < sizeof(tx_data) && tx_buffer.available() > 0) {
                int byte = tx_buffer.read();
                if (byte >= 0) {
                    tx_data[chunk_size++] = static_cast<uint8_t>(byte);
                }
            }
            if (chunk_size > 0) {
                uart_write_bytes(uart_num, reinterpret_cast<const char *>(tx_data), chunk_size);
            }
        } else {
            vTaskDelay(1); // Small delay when no data to avoid busy waiting
        }
    }
}

void Serial::write_to_buffer(const char *data, size_t len) const {
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data);
    size_t written = 0;

    // Try writing with a timeout to handle buffer full condition
    TickType_t start = xTaskGetTickCount();
    while (written < len) {
        if (tx_buffer.write(bytes[written])) {
            written++;
        } else {
            if ((xTaskGetTickCount() - start) * portTICK_PERIOD_MS > 1000) { // 1 second timeout
                throw std::runtime_error("transmit buffer full timeout");
            }
            vTaskDelay(1);
        }
    }
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
    if (!tx_buffer.write(byte)) {
        throw std::runtime_error("transmit buffer full");
    }
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
            write_to_buffer(&message[start], i - start);
            write_to_buffer(checksum_buffer, 4);
            start = i + 1;
            checksum = 0;
        } else {
            checksum ^= message[i];
        }
    }
}

int Serial::available() const {
    return rx_buffer.available();
}

bool Serial::has_buffered_lines() const {
    return rx_buffer.find_pattern('\n') != -1;
}

void Serial::flush() const {
    uart_flush(this->uart_num);
}

int Serial::read(uint32_t timeout) const {
    TickType_t start = xTaskGetTickCount();
    while (rx_buffer.available() == 0) {
        if (timeout == 0 || (xTaskGetTickCount() - start) * portTICK_PERIOD_MS >= timeout) {
            return -1;
        }
        vTaskDelay(1);
    }
    return rx_buffer.read();
}

int Serial::read_line(char *buffer, size_t buffer_len) const {
    int newline_pos = rx_buffer.find_pattern('\n');
    if (newline_pos < 0 || static_cast<size_t>(newline_pos) >= buffer_len) {
        return 0;
    }

    size_t read_len = rx_buffer.read(reinterpret_cast<uint8_t *>(buffer), newline_pos + 1);
    buffer[read_len] = '\0';
    return read_len;
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
