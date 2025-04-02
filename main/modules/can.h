#pragma once

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "module.h"
#include <memory>

class Can;
using Can_ptr = std::shared_ptr<Can>;

// CAN message structure for the send queue
struct CanMessage {
    uint32_t id;
    uint8_t data[8];
    uint8_t dlc;
    bool rtr;
};

// Output message structure for the output queue
struct CanOutputMessage {
    uint32_t id;
    uint8_t data[8];
    uint8_t dlc;
    bool rtr;
    char module_name[32]; // Buffer for storing the module name
};

class Can : public Module {
private:
    std::map<uint32_t, Module_ptr> subscribers;

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

    // Internal receive helper that doesn't generate output
    bool receive_internal(bool generate_output);

public:
    Can(const std::string name, const gpio_num_t rx_pin, const gpio_num_t tx_pin, const long baud_rate);
    ~Can();
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

    bool receive();
    void send(const uint32_t id, const uint8_t data[8], const bool rtr = false, const uint8_t dlc = 8) const;
    void send(const uint32_t id,
              const uint8_t d0, const uint8_t d1, const uint8_t d2, const uint8_t d3,
              const uint8_t d4, const uint8_t d5, const uint8_t d6, const uint8_t d7,
              const bool rtr = false) const;
    void subscribe(const uint32_t id, const Module_ptr module);
};
