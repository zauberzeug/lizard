#pragma once

#include "ethernet.h"
#include "module.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class EthernetBus;
using EthernetBus_ptr = std::shared_ptr<EthernetBus>;
using ConstEthernetBus_ptr = std::shared_ptr<const EthernetBus>;

class EthernetBus : public Module {
public:
    static inline constexpr const char *TYPE = "EthernetBus";

    EthernetBus(const std::string name, ConstEthernet_ptr ethernet);

    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

private:
    struct Peer {
        std::string name;
        std::string ip;
        uint16_t port;
    };

    const ConstEthernet_ptr ethernet;
    std::vector<Peer> peers;

    const Peer *find_peer(const std::string &name) const;
    void send_to_peer(const Peer &peer, const std::string &payload) const;
};
