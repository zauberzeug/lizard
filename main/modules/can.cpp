#include "can.h"
#include "../utils/uart.h"
#include "driver/twai.h"

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

    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_ERROR_CHECK(twai_start());
}

void Can::step() {
    while (this->receive()) {
    }
    Module::step();
}

bool Can::receive() {
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

    if (this->output_on) {
        static char buffer[256];
        int pos = std::sprintf(buffer, "can %03x", message.identifier);
        if (!(message.flags & TWAI_MSG_FLAG_RTR)) {
            for (int i = 0; i < message.data_length_code; ++i) {
                pos += std::sprintf(&buffer[pos], ",%02x", message.data[i]);
            }
        }
        echo(buffer);
    }

    return true;
}

void Can::send(const uint32_t id, const uint8_t data[8], const bool rtr, uint8_t dlc) const {
    twai_message_t message;
    message.identifier = id;
    message.flags = rtr ? TWAI_MSG_FLAG_RTR : TWAI_MSG_FLAG_NONE;
    message.data_length_code = dlc;
    for (int i = 0; i < dlc; ++i) {
        message.data[i] = data[i];
    }
    if (twai_transmit(&message, pdMS_TO_TICKS(0)) != ESP_OK) {
        throw std::runtime_error("could not send CAN message");
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
        twai_status_info_t status_info;
        if (twai_get_status_info(&status_info) != ESP_OK) {
            throw std::runtime_error("could not get status info");
        }
        echo("state:            %s", status_info.state == TWAI_STATE_STOPPED      ? "STOPPED"
                                     : status_info.state == TWAI_STATE_RUNNING    ? "RUNNING"
                                     : status_info.state == TWAI_STATE_BUS_OFF    ? "BUS_OFF"
                                     : status_info.state == TWAI_STATE_RECOVERING ? "RECOVERING"
                                                                                  : "UNKNOWN");
        echo("msgs_to_tx:       %d", status_info.msgs_to_tx);
        echo("msgs_to_rx:       %d", status_info.msgs_to_rx);
        echo("tx_error_counter: %d", status_info.tx_error_counter);
        echo("rx_error_counter: %d", status_info.rx_error_counter);
        echo("tx_failed_count:  %d", status_info.tx_failed_count);
        echo("rx_missed_count:  %d", status_info.rx_missed_count);
        echo("rx_overrun_count: %d", status_info.rx_overrun_count);
        echo("arb_lost_count:   %d", status_info.arb_lost_count);
        echo("bus_error_count:  %d", status_info.bus_error_count);
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
