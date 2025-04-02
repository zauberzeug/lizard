#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "module.h"
#include <memory>
#include <string>

class Serial;
using Serial_ptr = std::shared_ptr<Serial>;
using ConstSerial_ptr = std::shared_ptr<const Serial>;

// Structure for serial write data
struct SerialWriteData {
    uint8_t data[256]; // Buffer for data to write
    size_t length;     // Length of data to write
    bool is_byte;      // True if single byte, false if buffer
};

// Structure for serial output (for echoing)
struct SerialOutputData {
    char module_name[32]; // Buffer for module name
    uint8_t data[256];    // Buffer for output data
    size_t length;        // Length of output data
};

class Serial : public Module {
private:
    // Task handles
    TaskHandle_t rx_task_handle = nullptr;
    TaskHandle_t tx_task_handle = nullptr;

    // Message queues
    QueueHandle_t tx_queue = nullptr;
    QueueHandle_t output_queue = nullptr;

    // Flag to control task operation
    volatile bool tasks_running = false;

    // Static task functions
    static void rx_task_function(void *arg);
    static void tx_task_function(void *arg);

    // Task initialization and cleanup
    void initialize_tasks();
    void cleanup_tasks();

    // Internal read helper that doesn't generate output
    int read_internal(uint32_t timeout, bool generate_output) const;

public:
    const gpio_num_t rx_pin;
    const gpio_num_t tx_pin;
    const long baud_rate;
    const uart_port_t uart_num;

    Serial(const std::string name,
           const gpio_num_t rx_pin, const gpio_num_t tx_pin, const long baud_rate, const uart_port_t uart_num);
    ~Serial();
    void step() override;
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
