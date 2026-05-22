#pragma once

#include "driver/gpio.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "module.h"
#include <memory>
#include <string>

class Ethernet;
using Ethernet_ptr = std::shared_ptr<Ethernet>;
using ConstEthernet_ptr = std::shared_ptr<const Ethernet>;

class Ethernet : public Module {
public:
    Ethernet(const std::string name,
             gpio_num_t miso_pin,
             gpio_num_t mosi_pin,
             gpio_num_t sclk_pin,
             gpio_num_t cs_pin,
             gpio_num_t int_pin,
             gpio_num_t rst_pin,
             const std::string &ip,
             const std::string &gateway,
             const std::string &netmask);

    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

private:
    esp_eth_handle_t eth_handle = nullptr;
    esp_netif_t *netif = nullptr;
};
