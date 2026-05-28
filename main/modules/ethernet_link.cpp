#include "ethernet_link.h"

#include "../utils/uart.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "module_helpers.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>

extern void process_line(const char *line, const int len);

static Module_ptr create_ethernet_link(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler) {
    Module::expect(arguments, 2, identifier, integer);
    const Ethernet_ptr ethernet = get_module_argument<Ethernet>(arguments[0]);
    const long port = arguments[1]->evaluate_integer();
    if (port <= 0 || port > 65535) {
        throw std::runtime_error("port must be between 1 and 65535");
    }
    return std::make_shared<EthernetLink>(name, ethernet, static_cast<uint16_t>(port));
}
REGISTER_MODULE(EthernetLink, &create_ethernet_link)

static constexpr size_t OUTGOING_QUEUE_LENGTH = 32;
static constexpr size_t INCOMING_QUEUE_LENGTH = 32;

const std::map<std::string, Variable_ptr> EthernetLink::get_defaults() {
    return {
        {"clients", std::make_shared<IntegerVariable>()},
    };
}

EthernetLink::EthernetLink(const std::string name,
                           ConstEthernet_ptr ethernet,
                           uint16_t port)
    : Module(name), ethernet(ethernet), port(port) {
    this->properties = EthernetLink::get_defaults();

    for (size_t i = 0; i < MAX_CLIENTS; ++i) {
        this->client_fds[i] = -1;
    }

    this->incoming_queue = xQueueCreate(INCOMING_QUEUE_LENGTH, sizeof(IncomingLine));
    if (!this->incoming_queue) {
        throw std::runtime_error("could not create ethernet link incoming queue");
    }
    this->outgoing_queue = xQueueCreate(OUTGOING_QUEUE_LENGTH, sizeof(OutgoingLine));
    if (!this->outgoing_queue) {
        vQueueDelete(this->incoming_queue);
        throw std::runtime_error("could not create ethernet link outgoing queue");
    }

    this->server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (this->server_fd < 0) {
        throw std::runtime_error("could not create ethernet link server socket");
    }
    int yes = 1;
    setsockopt(this->server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(this->port);
    if (bind(this->server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        close(this->server_fd);
        throw std::runtime_error("could not bind ethernet link server socket");
    }
    if (listen(this->server_fd, MAX_CLIENTS) < 0) {
        close(this->server_fd);
        throw std::runtime_error("could not listen on ethernet link server socket");
    }

    if (xTaskCreatePinnedToCore(EthernetLink::task_trampoline, "ethernet_link", 8192, this, 5, &this->task_handle, 1) != pdPASS) {
        close(this->server_fd);
        throw std::runtime_error("could not start ethernet link task");
    }

    register_echo_callback([this](const char *line) { this->handle_echo(line); });
}

void EthernetLink::step() {
    IncomingLine line;
    while (xQueueReceive(this->incoming_queue, &line, 0) == pdTRUE) {
        bool ok = true;
        int len = check(line.data, line.length, &ok);
        if (!ok) {
            echo("warning: ethernet link %s checksum mismatch", this->name.c_str());
            continue;
        }
        try {
            process_line(line.data, len);
        } catch (const std::exception &e) {
            echo("error in ethernet link %s: %s", this->name.c_str(), e.what());
        }
    }
    Module::step();
}

void EthernetLink::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    Module::call(method_name, arguments);
}

void EthernetLink::handle_echo(const char *line) {
    OutgoingLine msg;
    const size_t len = std::strlen(line);
    if (len + 2 >= LINE_BUFFER_SIZE) {
        return;
    }
    std::memcpy(msg.data, line, len);
    msg.data[len] = '\n';
    msg.length = len + 1;
    xQueueSend(this->outgoing_queue, &msg, 0);
}

void EthernetLink::task_trampoline(void *arg) {
    static_cast<EthernetLink *>(arg)->task_loop();
}

void EthernetLink::task_loop() {
    while (true) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(this->server_fd, &read_fds);
        int max_fd = this->server_fd;
        for (size_t i = 0; i < MAX_CLIENTS; ++i) {
            if (this->client_fds[i] >= 0) {
                FD_SET(this->client_fds[i], &read_fds);
                if (this->client_fds[i] > max_fd) {
                    max_fd = this->client_fds[i];
                }
            }
        }

        timeval timeout = {0, 50 * 1000};
        const int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);

        if (activity > 0) {
            if (FD_ISSET(this->server_fd, &read_fds)) {
                this->accept_client();
            }
            for (size_t i = 0; i < MAX_CLIENTS; ++i) {
                if (this->client_fds[i] >= 0 && FD_ISSET(this->client_fds[i], &read_fds)) {
                    this->read_client(i);
                }
            }
        }

        this->drain_outgoing();
    }
}

void EthernetLink::accept_client() {
    sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    const int new_fd = accept(this->server_fd, reinterpret_cast<sockaddr *>(&client_addr), &addr_len);
    if (new_fd < 0) {
        return;
    }

    for (size_t i = 0; i < MAX_CLIENTS; ++i) {
        if (this->client_fds[i] < 0) {
            int yes = 1;
            setsockopt(new_fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
            this->client_fds[i] = new_fd;
            this->client_buffer_lengths[i] = 0;
            this->get_property("clients")->integer_value++;
            return;
        }
    }
    // no slot free
    close(new_fd);
}

void EthernetLink::read_client(size_t slot) {
    const int fd = this->client_fds[slot];
    char chunk[LINE_BUFFER_SIZE];
    const int n = recv(fd, chunk, sizeof(chunk), 0);
    if (n <= 0) {
        this->close_client(slot);
        return;
    }

    for (int i = 0; i < n; ++i) {
        const char c = chunk[i];
        if (c == '\n' || c == '\r') {
            if (this->client_buffer_lengths[slot] > 0) {
                IncomingLine line;
                line.length = this->client_buffer_lengths[slot];
                std::memcpy(line.data, this->client_buffers[slot], line.length);
                line.data[line.length] = '\0';
                if (xQueueSend(this->incoming_queue, &line, 0) != pdTRUE) {
                    // drop line silently — main task is overloaded
                }
                this->client_buffer_lengths[slot] = 0;
            }
        } else if (this->client_buffer_lengths[slot] < LINE_BUFFER_SIZE - 1) {
            this->client_buffers[slot][this->client_buffer_lengths[slot]++] = c;
        } else {
            // line too long, drop buffer
            this->client_buffer_lengths[slot] = 0;
        }
    }
}

void EthernetLink::close_client(size_t slot) {
    if (this->client_fds[slot] >= 0) {
        close(this->client_fds[slot]);
        this->client_fds[slot] = -1;
        this->client_buffer_lengths[slot] = 0;
        if (this->get_property("clients")->integer_value > 0) {
            this->get_property("clients")->integer_value--;
        }
    }
}

void EthernetLink::drain_outgoing() {
    OutgoingLine msg;
    while (xQueueReceive(this->outgoing_queue, &msg, 0) == pdTRUE) {
        for (size_t i = 0; i < MAX_CLIENTS; ++i) {
            if (this->client_fds[i] < 0) {
                continue;
            }
            const int sent = send(this->client_fds[i], msg.data, msg.length, MSG_DONTWAIT);
            if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                this->close_client(i);
            }
        }
    }
}
