#include "ethernet_bus.h"

#include "../utils/format.h"
#include "../utils/uart.h"
#include "lwip/sockets.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>

REGISTER_MODULE_DEFAULTS(EthernetBus)

static constexpr uint16_t DEFAULT_PEER_PORT = 8080;
static constexpr int CONNECT_TIMEOUT_MS = 5000;

const std::map<std::string, Variable_ptr> EthernetBus::get_defaults() {
    return {};
}

EthernetBus::EthernetBus(const std::string name, ConstEthernet_ptr ethernet)
    : Module(ethernet_bus, name), ethernet(ethernet) {
    this->properties = EthernetBus::get_defaults();
}

const EthernetBus::Peer *EthernetBus::find_peer(const std::string &name) const {
    for (const auto &p : this->peers) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

void EthernetBus::send_to_peer(const Peer &peer, const std::string &payload) const {
    const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        throw std::runtime_error("could not create peer socket");
    }

    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(peer.port);
    if (inet_pton(AF_INET, peer.ip.c_str(), &addr.sin_addr) != 1) {
        close(fd);
        throw std::runtime_error("invalid peer IP: " + peer.ip);
    }

    const int rc = connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    const int connect_errno = errno;
    if (rc < 0 && connect_errno != EINPROGRESS) {
        close(fd);
        throw std::runtime_error("connect to peer " + peer.name + " failed (errno=" + std::to_string(connect_errno) + ")");
    }

    if (rc < 0) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        timeval tv = {CONNECT_TIMEOUT_MS / 1000, (CONNECT_TIMEOUT_MS % 1000) * 1000};
        const int ready = select(fd + 1, nullptr, &wfds, nullptr, &tv);
        if (ready < 0) {
            const int select_errno = errno;
            close(fd);
            throw std::runtime_error("connect to peer " + peer.name + " select failed (errno=" + std::to_string(select_errno) + ")");
        }
        if (ready == 0) {
            close(fd);
            throw std::runtime_error("connect to peer " + peer.name + " timed out");
        }
        int sock_err = 0;
        socklen_t err_len = sizeof(sock_err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &sock_err, &err_len);
        if (sock_err != 0) {
            close(fd);
            throw std::runtime_error("connect to peer " + peer.name + " refused (sock_err=" + std::to_string(sock_err) + ")");
        }
    }

    const ssize_t written = send(fd, payload.c_str(), payload.size(), 0);
    if (written < 0 || static_cast<size_t>(written) != payload.size()) {
        close(fd);
        throw std::runtime_error("send to peer " + peer.name + " incomplete");
    }

    shutdown(fd, SHUT_WR);
    close(fd);
}

void EthernetBus::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "add") {
        if (arguments.size() != 2 && arguments.size() != 3) {
            throw std::runtime_error("add expects 2 or 3 arguments (name, ip[, port])");
        }
        if ((arguments[0]->type & string) == 0 || (arguments[1]->type & string) == 0) {
            throw std::runtime_error("name and ip must be strings");
        }
        if (arguments.size() == 3 && (arguments[2]->type & integer) == 0) {
            throw std::runtime_error("port must be an integer");
        }
        const std::string name = arguments[0]->evaluate_string();
        const std::string ip = arguments[1]->evaluate_string();
        const long port = arguments.size() == 3 ? arguments[2]->evaluate_integer() : DEFAULT_PEER_PORT;
        if (port <= 0 || port > 65535) {
            throw std::runtime_error("port must be between 1 and 65535");
        }
        if (this->find_peer(name) != nullptr) {
            throw std::runtime_error("peer '" + name + "' already exists");
        }
        this->peers.push_back({name, ip, static_cast<uint16_t>(port)});
    } else if (method_name == "remove") {
        Module::expect(arguments, 1, string);
        const std::string name = arguments[0]->evaluate_string();
        for (auto it = this->peers.begin(); it != this->peers.end(); ++it) {
            if (it->name == name) {
                this->peers.erase(it);
                return;
            }
        }
        throw std::runtime_error("unknown peer: " + name);
    } else if (method_name == "list") {
        Module::expect(arguments, 0);
        for (const auto &p : this->peers) {
            echo("peer %s -> %s:%u", p.name.c_str(), p.ip.c_str(), p.port);
        }
    } else if (method_name == "send") {
        if (arguments.size() < 2) {
            throw std::runtime_error("send expects at least 2 arguments (name, format[, args...])");
        }
        if ((arguments[0]->type & string) == 0) {
            throw std::runtime_error("name must be a string");
        }
        if ((arguments[1]->type & string) == 0) {
            throw std::runtime_error("format must be a string");
        }
        const std::string name = arguments[0]->evaluate_string();
        const Peer *target = this->find_peer(name);
        if (target == nullptr) {
            throw std::runtime_error("unknown peer: " + name);
        }
        std::string payload = format_args(arguments[1]->evaluate_string(), arguments, 2);
        payload.push_back('\n');
        this->send_to_peer(*target, payload);
    } else {
        Module::call(method_name, arguments);
    }
}
