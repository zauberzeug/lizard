#include "serial.h"
#include "utils/string_utils.h"
#include "utils/timing.h"
#include "utils/uart.h"
#include <cstring>
#include <stdexcept>

#define RX_BUF_SIZE 2048
#define TX_BUF_SIZE 2048
#define UART_PATTERN_QUEUE_SIZE 100
#define SERIAL_TX_QUEUE_SIZE 20
#define SERIAL_OUTPUT_QUEUE_SIZE 20
#define SERIAL_TASK_STACK_SIZE 4096
#define SERIAL_RX_TASK_PRIORITY 10
#define SERIAL_TX_TASK_PRIORITY 10

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
    this->initialize_tasks();
}

Serial::~Serial() {
    cleanup_tasks();
    this->deinstall();
}

void Serial::initialize_tasks() {
    // Create message queue for sending serial data
    tx_queue = xQueueCreate(SERIAL_TX_QUEUE_SIZE, sizeof(SerialWriteData));
    if (tx_queue == nullptr) {
        throw std::runtime_error("failed to create Serial TX queue");
    }

    // Create message queue for output data
    output_queue = xQueueCreate(SERIAL_OUTPUT_QUEUE_SIZE, sizeof(SerialOutputData));
    if (output_queue == nullptr) {
        vQueueDelete(tx_queue);
        tx_queue = nullptr;
        throw std::runtime_error("failed to create Serial output queue");
    }

    // Set the tasks_running flag
    tasks_running = true;

    // Create receive task on Core 1
    BaseType_t status = xTaskCreatePinnedToCore(
        rx_task_function,
        "serial_rx_task",
        SERIAL_TASK_STACK_SIZE,
        this,
        SERIAL_RX_TASK_PRIORITY,
        &rx_task_handle,
        1); // Pin to Core 1
    if (status != pdPASS) {
        cleanup_tasks();
        throw std::runtime_error("failed to create Serial RX task");
    }

    // Create transmit task on Core 1
    status = xTaskCreatePinnedToCore(
        tx_task_function,
        "serial_tx_task",
        SERIAL_TASK_STACK_SIZE,
        this,
        SERIAL_TX_TASK_PRIORITY,
        &tx_task_handle,
        1); // Pin to Core 1
    if (status != pdPASS) {
        cleanup_tasks();
        throw std::runtime_error("failed to create Serial TX task");
    }
}

void Serial::cleanup_tasks() {
    // Signal tasks to stop
    tasks_running = false;

    // Delete tasks if they exist
    if (rx_task_handle != nullptr) {
        vTaskDelete(rx_task_handle);
        rx_task_handle = nullptr;
    }

    if (tx_task_handle != nullptr) {
        vTaskDelete(tx_task_handle);
        tx_task_handle = nullptr;
    }

    // Delete the queues if they exist
    if (tx_queue != nullptr) {
        vQueueDelete(tx_queue);
        tx_queue = nullptr;
    }

    if (output_queue != nullptr) {
        vQueueDelete(output_queue);
        output_queue = nullptr;
    }
}

void Serial::rx_task_function(void *arg) {
    Serial *serial_instance = static_cast<Serial *>(arg);
    uint8_t byte_data;

    while (serial_instance->tasks_running) {
        // Check if there's any data available
        if (serial_instance->available() > 0) {
            // Read one byte at a time with minimal timeout
            int result = uart_read_bytes(serial_instance->uart_num, &byte_data, 1, 1);

            if (result > 0) {
                // If we're outputting data, queue it for the main task
                if (serial_instance->output_on && serial_instance->output_queue != nullptr) {
                    SerialOutputData output_data;
                    output_data.data[0] = byte_data;
                    output_data.length = 1;

                    // Copy the module name
                    strncpy(output_data.module_name, serial_instance->name.c_str(), sizeof(output_data.module_name) - 1);
                    output_data.module_name[sizeof(output_data.module_name) - 1] = '\0';

                    // Try to send to queue, don't block if full
                    xQueueSend(serial_instance->output_queue, &output_data, 0);
                }
            }
        } else {
            // No data available, sleep a little to avoid hogging CPU
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    // Task will be deleted by the creator
    vTaskDelete(NULL);
}

void Serial::tx_task_function(void *arg) {
    Serial *serial_instance = static_cast<Serial *>(arg);
    SerialWriteData write_data;

    while (serial_instance->tasks_running) {
        if (xQueueReceive(serial_instance->tx_queue, &write_data, pdMS_TO_TICKS(10)) == pdTRUE) {
            // Write the data to the UART
            if (write_data.is_byte) {
                // Single byte write
                uart_write_bytes(serial_instance->uart_num, (const char *)&write_data.data[0], 1);
            } else {
                // Multiple bytes write
                uart_write_bytes(serial_instance->uart_num, (const char *)write_data.data, write_data.length);
            }
        }
    }

    // Task will be deleted by the creator
    vTaskDelete(NULL);
}

void Serial::step() {
    // Process any pending output messages
    if (output_queue != nullptr) {
        SerialOutputData output_data;
        while (xQueueReceive(output_queue, &output_data, 0) == pdTRUE) {
            // Format and echo the message
            static char buffer[512];
            int pos = 0;

            // Format as "module_name byte1 byte2 byte3..."
            pos += csprintf(buffer, sizeof(buffer), "%s", output_data.module_name);

            for (size_t i = 0; i < output_data.length; ++i) {
                pos += csprintf(&buffer[pos], sizeof(buffer) - pos, " %02x", output_data.data[i]);
            }

            echo(buffer);
        }
    }

    Module::step();
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
    // Tasks need to be reinitialized, but this is const method, so we can't do it here
    // The caller should handle this
}

size_t Serial::write(const uint8_t byte) const {
    if (tx_queue == nullptr) {
        throw std::runtime_error("Serial TX queue not initialized");
    }

    // Create a write data structure
    SerialWriteData write_data;
    write_data.data[0] = byte;
    write_data.length = 1;
    write_data.is_byte = true;

    // Send to queue with timeout
    if (xQueueSend(tx_queue, &write_data, pdMS_TO_TICKS(100)) != pdTRUE) {
        throw std::runtime_error("failed to queue Serial write data (queue full)");
    }

    return 1;
}

void Serial::write_checked_line(const char *message) const {
    this->write_checked_line(message, std::strlen(message));
}

void Serial::write_checked_line(const char *message, const int length) const {
    if (tx_queue == nullptr) {
        throw std::runtime_error("Serial TX queue not initialized");
    }

    static char checksum_buffer[16];
    uint8_t checksum = 0;
    int start = 0;

    for (unsigned int i = 0; i < length + 1; ++i) {
        if (i >= length || message[i] == '\n') {
            // Create a write data structure for the message segment
            SerialWriteData write_data;
            write_data.is_byte = false;

            // Copy the message segment
            if (i - start > 0) {
                size_t segment_len = i - start;
                if (segment_len > sizeof(write_data.data) - 4) { // Leave room for checksum
                    segment_len = sizeof(write_data.data) - 4;
                }
                memcpy(write_data.data, &message[start], segment_len);
                write_data.length = segment_len;

                // Send to queue with timeout
                if (xQueueSend(tx_queue, &write_data, pdMS_TO_TICKS(100)) != pdTRUE) {
                    throw std::runtime_error("failed to queue Serial write data (queue full)");
                }
            }

            // Create a write data structure for the checksum
            SerialWriteData checksum_data;
            checksum_data.is_byte = false;

            // Format and copy the checksum
            csprintf(checksum_buffer, sizeof(checksum_buffer), "@%02x\n", checksum);
            memcpy(checksum_data.data, checksum_buffer, 4);
            checksum_data.length = 4;

            // Send to queue with timeout
            if (xQueueSend(tx_queue, &checksum_data, pdMS_TO_TICKS(100)) != pdTRUE) {
                throw std::runtime_error("failed to queue Serial write data (queue full)");
            }

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

int Serial::read_internal(uint32_t timeout, bool generate_output) const {
    uint8_t data = 0;
    const int length = uart_read_bytes(this->uart_num, &data, 1, timeout);

    if (length > 0 && generate_output && this->output_on && output_queue != nullptr) {
        // Queue the data for output
        SerialOutputData output_data;
        output_data.data[0] = data;
        output_data.length = 1;

        // Copy the module name
        strncpy(output_data.module_name, this->name.c_str(), sizeof(output_data.module_name) - 1);
        output_data.module_name[sizeof(output_data.module_name) - 1] = '\0';

        // Try to send to queue, don't block if full
        xQueueSend(output_queue, &output_data, 0);
    }

    return length > 0 ? data : -1;
}

int Serial::read(uint32_t timeout) const {
    return read_internal(timeout, false);
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
