#include "can.h"
#include "../utils/string_utils.h"
#include "../utils/uart.h"
#include "driver/twai.h"
#include <cstring>
#include <stdexcept>

#define CAN_TX_QUEUE_SIZE 20
#define CAN_OUTPUT_QUEUE_SIZE 20
#define CAN_TASK_STACK_SIZE 4096
#define CAN_RX_TASK_PRIORITY 10
#define CAN_TX_TASK_PRIORITY 10

REGISTER_MODULE_DEFAULTS(Can)

const std::map<std::string, Variable_ptr> Can::get_defaults() {
    return {
        {"state", std::make_shared<StringVariable>()},
        {"tx_error_counter", std::make_shared<IntegerVariable>()},
        {"rx_error_counter", std::make_shared<IntegerVariable>()},
        {"msgs_to_tx", std::make_shared<IntegerVariable>()},
        {"msgs_to_rx", std::make_shared<IntegerVariable>()},
        {"tx_failed_count", std::make_shared<IntegerVariable>()},
        {"rx_missed_count", std::make_shared<IntegerVariable>()},
        {"rx_overrun_count", std::make_shared<IntegerVariable>()},
        {"arb_lost_count", std::make_shared<IntegerVariable>()},
        {"bus_error_count", std::make_shared<IntegerVariable>()},
    };
}

Can::Can(const std::string name, const gpio_num_t rx_pin, const gpio_num_t tx_pin, const long baud_rate)
    : Module(can, name) {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(tx_pin, rx_pin, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config;
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    switch (baud_rate) {
    case 1000000:
        t_config = TWAI_TIMING_CONFIG_1MBITS();
        break;
    case 800000:
        t_config = TWAI_TIMING_CONFIG_800KBITS();
        break;
    case 500000:
        t_config = TWAI_TIMING_CONFIG_500KBITS();
        break;
    case 250000:
        t_config = TWAI_TIMING_CONFIG_250KBITS();
        break;
    case 125000:
        t_config = TWAI_TIMING_CONFIG_125KBITS();
        break;
    case 100000:
        t_config = TWAI_TIMING_CONFIG_100KBITS();
        break;
    case 50000:
        t_config = TWAI_TIMING_CONFIG_50KBITS();
        break;
    case 25000:
        t_config = TWAI_TIMING_CONFIG_25KBITS();
        break;
    default:
        throw std::runtime_error("invalid baud rate");
    }

    g_config.rx_queue_len = 20;
    g_config.tx_queue_len = 20;

    this->properties = Can::get_defaults();

    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_ERROR_CHECK(twai_start());

    // Initialize tasks after driver is installed and started
    initialize_tasks();
}

Can::~Can() {
    cleanup_tasks();

    // Try to stop and uninstall the TWAI driver
    if (twai_stop() == ESP_OK) {
        twai_driver_uninstall();
    }
}

void Can::initialize_tasks() {
    // Create message queue for sending CAN messages
    tx_queue = xQueueCreate(CAN_TX_QUEUE_SIZE, sizeof(CanMessage));
    if (tx_queue == nullptr) {
        throw std::runtime_error("failed to create CAN TX queue");
    }

    // Create message queue for output messages
    output_queue = xQueueCreate(CAN_OUTPUT_QUEUE_SIZE, sizeof(CanOutputMessage));
    if (output_queue == nullptr) {
        vQueueDelete(tx_queue);
        tx_queue = nullptr;
        throw std::runtime_error("failed to create CAN output queue");
    }

    // Set the tasks_running flag
    tasks_running = true;

    // Create receive task on Core 1
    BaseType_t status = xTaskCreatePinnedToCore(
        rx_task_function,
        "can_rx_task",
        CAN_TASK_STACK_SIZE,
        this,
        CAN_RX_TASK_PRIORITY,
        &rx_task_handle,
        1); // Pin to Core 1
    if (status != pdPASS) {
        cleanup_tasks();
        throw std::runtime_error("failed to create CAN RX task");
    }

    // Create transmit task on Core 1
    status = xTaskCreatePinnedToCore(
        tx_task_function,
        "can_tx_task",
        CAN_TASK_STACK_SIZE,
        this,
        CAN_TX_TASK_PRIORITY,
        &tx_task_handle,
        1); // Pin to Core 1
    if (status != pdPASS) {
        cleanup_tasks();
        throw std::runtime_error("failed to create CAN TX task");
    }
}

void Can::cleanup_tasks() {
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

void Can::rx_task_function(void *arg) {
    Can *can_instance = static_cast<Can *>(arg);

    while (can_instance->tasks_running) {
        // Call the receive method which handles the message but doesn't output
        // This no longer blocks the main thread or calls echo()
        can_instance->receive_internal(true);

        // Small delay to prevent CPU hogging
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    // Task will be deleted by the creator
    vTaskDelete(NULL);
}

void Can::tx_task_function(void *arg) {
    Can *can_instance = static_cast<Can *>(arg);
    CanMessage msg;

    while (can_instance->tasks_running) {
        if (xQueueReceive(can_instance->tx_queue, &msg, pdMS_TO_TICKS(10)) == pdTRUE) {
            // Prepare and send the CAN message
            twai_message_t twai_msg;
            twai_msg.identifier = msg.id;
            twai_msg.flags = msg.rtr ? TWAI_MSG_FLAG_RTR : TWAI_MSG_FLAG_NONE;
            twai_msg.data_length_code = msg.dlc;

            for (int i = 0; i < msg.dlc; ++i) {
                twai_msg.data[i] = msg.data[i];
            }

            // Try to send with retry mechanism
            esp_err_t err = twai_transmit(&twai_msg, pdMS_TO_TICKS(100));
            if (err != ESP_OK) {
                // Try to recover from errors
                twai_status_info_t status_info;
                if (twai_get_status_info(&status_info) == ESP_OK) {
                    if (status_info.state == TWAI_STATE_BUS_OFF) {
                        twai_initiate_recovery();
                        vTaskDelay(pdMS_TO_TICKS(100)); // Wait for recovery to start
                    }

                    if (status_info.state != TWAI_STATE_RUNNING) {
                        twai_stop();
                        vTaskDelay(pdMS_TO_TICKS(10));
                        twai_start();
                    }
                }

                // Try to resend once more
                twai_transmit(&twai_msg, pdMS_TO_TICKS(100));
            }
        }
    }

    // Task will be deleted by the creator
    vTaskDelete(NULL);
}

void Can::step() {
    // We don't need to poll for receive messages anymore, as it's done in the RX task
    // We still need to update the status info and process any output messages

    // Process any pending output messages
    if (output_queue != nullptr) {
        CanOutputMessage output_msg;
        while (xQueueReceive(output_queue, &output_msg, 0) == pdTRUE) {
            // Format and echo the message
            static char buffer[256];
            int pos = csprintf(buffer, sizeof(buffer), "%s %03lx", output_msg.module_name, output_msg.id);
            if (!output_msg.rtr) {
                for (int i = 0; i < output_msg.dlc; ++i) {
                    pos += csprintf(&buffer[pos], sizeof(buffer) - pos, ",%02x", output_msg.data[i]);
                }
            }
            echo(buffer);
        }
    }

    twai_status_info_t status_info;
    if (twai_get_status_info(&status_info) != ESP_OK) {
        throw std::runtime_error("could not get status info");
    }
    this->properties.at("state")->string_value = status_info.state == TWAI_STATE_STOPPED      ? "STOPPED"
                                                 : status_info.state == TWAI_STATE_RUNNING    ? "RUNNING"
                                                 : status_info.state == TWAI_STATE_BUS_OFF    ? "BUS_OFF"
                                                 : status_info.state == TWAI_STATE_RECOVERING ? "RECOVERING"
                                                                                              : "UNKNOWN";
    this->properties.at("tx_error_counter")->integer_value = status_info.tx_error_counter;
    this->properties.at("rx_error_counter")->integer_value = status_info.rx_error_counter;
    this->properties.at("msgs_to_tx")->integer_value = status_info.msgs_to_tx;
    this->properties.at("msgs_to_rx")->integer_value = status_info.msgs_to_rx;
    this->properties.at("tx_failed_count")->integer_value = status_info.tx_failed_count;
    this->properties.at("rx_missed_count")->integer_value = status_info.rx_missed_count;
    this->properties.at("rx_overrun_count")->integer_value = status_info.rx_overrun_count;
    this->properties.at("arb_lost_count")->integer_value = status_info.arb_lost_count;
    this->properties.at("bus_error_count")->integer_value = status_info.bus_error_count;

    Module::step();
}

bool Can::receive_internal(bool generate_output) {
    twai_message_t message;
    if (twai_receive(&message, pdMS_TO_TICKS(0)) != ESP_OK) {
        return false;
    }

    if (this->subscribers.count(message.identifier)) {
        this->subscribers[message.identifier]->handle_can_msg(
            message.identifier,
            message.data_length_code,
            message.data);
    }

    // If output is enabled and we should generate output, queue it instead of echoing directly
    if (this->output_on && generate_output && output_queue != nullptr) {
        CanOutputMessage output_msg;
        output_msg.id = message.identifier;
        output_msg.dlc = message.data_length_code;
        output_msg.rtr = (message.flags & TWAI_MSG_FLAG_RTR) != 0;

        // Copy the module name
        strncpy(output_msg.module_name, this->name.c_str(), sizeof(output_msg.module_name) - 1);
        output_msg.module_name[sizeof(output_msg.module_name) - 1] = '\0';

        // Copy data
        for (int i = 0; i < message.data_length_code; ++i) {
            output_msg.data[i] = message.data[i];
        }

        // Send to queue, don't wait if queue is full
        xQueueSend(output_queue, &output_msg, 0);
    }

    return true;
}

bool Can::receive() {
    // This is now just a wrapper around receive_internal
    // Used by the main task if it wants to manually check for messages
    return receive_internal(false);
}

void Can::send(const uint32_t id, const uint8_t data[8], const bool rtr, uint8_t dlc) const {
    if (tx_queue == nullptr) {
        throw std::runtime_error("CAN TX queue not initialized");
    }

    // Create a CanMessage structure
    CanMessage msg;
    msg.id = id;
    msg.dlc = dlc;
    msg.rtr = rtr;

    // Copy data
    for (int i = 0; i < dlc; ++i) {
        msg.data[i] = data[i];
    }

    // Send to queue with timeout
    if (xQueueSend(tx_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        throw std::runtime_error("failed to queue CAN message (queue full)");
    }
}

void Can::send(uint32_t id,
               uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
               uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7,
               bool rtr) const {
    uint8_t data[8] = {d0, d1, d2, d3, d4, d5, d6, d7};
    this->send(id, data, rtr);
}

void Can::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "send") {
        Module::expect(arguments, 9, integer, integer, integer, integer, integer, integer, integer, integer, integer);
        this->send(arguments[0]->evaluate_integer(),
                   arguments[1]->evaluate_integer(),
                   arguments[2]->evaluate_integer(),
                   arguments[3]->evaluate_integer(),
                   arguments[4]->evaluate_integer(),
                   arguments[5]->evaluate_integer(),
                   arguments[6]->evaluate_integer(),
                   arguments[7]->evaluate_integer(),
                   arguments[8]->evaluate_integer());
    } else if (method_name == "get_status") {
        Module::expect(arguments, 0);
        echo("state:            %s", this->properties.at("state")->string_value.c_str());
        echo("msgs_to_tx:       %d", (int)this->properties.at("msgs_to_tx")->integer_value);
        echo("msgs_to_rx:       %d", (int)this->properties.at("msgs_to_rx")->integer_value);
        echo("tx_error_counter: %d", (int)this->properties.at("tx_error_counter")->integer_value);
        echo("rx_error_counter: %d", (int)this->properties.at("rx_error_counter")->integer_value);
        echo("tx_failed_count:  %d", (int)this->properties.at("tx_failed_count")->integer_value);
        echo("rx_missed_count:  %d", (int)this->properties.at("rx_missed_count")->integer_value);
        echo("rx_overrun_count: %d", (int)this->properties.at("rx_overrun_count")->integer_value);
        echo("arb_lost_count:   %d", (int)this->properties.at("arb_lost_count")->integer_value);
        echo("bus_error_count:  %d", (int)this->properties.at("bus_error_count")->integer_value);
    } else if (method_name == "start") {
        Module::expect(arguments, 0);
        if (twai_start() != ESP_OK) {
            throw std::runtime_error("could not start twai driver");
        }
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        if (twai_stop() != ESP_OK) {
            throw std::runtime_error("could not stop twai driver");
        }
    } else if (method_name == "recover") {
        Module::expect(arguments, 0);
        if (twai_initiate_recovery() != ESP_OK) {
            throw std::runtime_error("could not initiate recovery");
        }
    } else {
        Module::call(method_name, arguments);
    }
}

void Can::subscribe(const uint32_t id, const Module_ptr module) {
    if (this->subscribers.count(id)) {
        throw std::runtime_error("there is already a subscriber for this CAN ID");
    }
    this->subscribers[id] = module;
}
