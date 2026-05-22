#pragma once

#include "ethernet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "module.h"
#include <cstdint>
#include <memory>
#include <string>

class EthernetLink;
using EthernetLink_ptr = std::shared_ptr<EthernetLink>;
using ConstEthernetLink_ptr = std::shared_ptr<const EthernetLink>;

class EthernetLink : public Module {
public:
    static constexpr size_t LINE_BUFFER_SIZE = 1024;
    static constexpr size_t MAX_CLIENTS = 4;

    EthernetLink(const std::string name,
                 ConstEthernet_ptr ethernet,
                 uint16_t port);

    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

private:
    struct IncomingLine {
        size_t length;
        char data[LINE_BUFFER_SIZE];
    };
    struct OutgoingLine {
        size_t length;
        char data[LINE_BUFFER_SIZE];
    };

    const ConstEthernet_ptr ethernet;
    const uint16_t port;

    int server_fd = -1;
    int client_fds[MAX_CLIENTS];
    size_t client_buffer_lengths[MAX_CLIENTS] = {};
    char client_buffers[MAX_CLIENTS][LINE_BUFFER_SIZE];

    QueueHandle_t incoming_queue = nullptr;
    QueueHandle_t outgoing_queue = nullptr;
    TaskHandle_t task_handle = nullptr;

    static void task_trampoline(void *arg);
    void task_loop();
    void accept_client();
    void read_client(size_t slot);
    void close_client(size_t slot);
    void drain_outgoing();
    void handle_echo(const char *line);
};
