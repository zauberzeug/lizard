#include "ethernet.h"

#include "../utils/uart.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "lwip/ip4_addr.h"
#include <cstring>
#include <stdexcept>

REGISTER_MODULE_DEFAULTS(Ethernet)

const std::map<std::string, Variable_ptr> Ethernet::get_defaults() {
    return {
        {"link", std::make_shared<BooleanVariable>()},
        {"has_ip", std::make_shared<BooleanVariable>()},
    };
}

static bool netif_initialized = false;

static void on_eth_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    Ethernet *self = static_cast<Ethernet *>(arg);
    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        self->get_property("link")->boolean_value = true;
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        self->get_property("link")->boolean_value = false;
        self->get_property("has_ip")->boolean_value = false;
        break;
    default:
        break;
    }
}

static void on_ip_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    Ethernet *self = static_cast<Ethernet *>(arg);
    if (event_id == IP_EVENT_ETH_GOT_IP) {
        self->get_property("has_ip")->boolean_value = true;
    }
}

Ethernet::Ethernet(const std::string name,
                   gpio_num_t miso_pin,
                   gpio_num_t mosi_pin,
                   gpio_num_t sclk_pin,
                   gpio_num_t cs_pin,
                   gpio_num_t int_pin,
                   gpio_num_t rst_pin,
                   const std::string &ip,
                   const std::string &gateway,
                   const std::string &netmask)
    : Module(ethernet, name) {
    this->properties = Ethernet::get_defaults();

    if (!netif_initialized) {
        if (esp_netif_init() != ESP_OK) {
            throw std::runtime_error("could not initialize netif");
        }
        esp_err_t err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            throw std::runtime_error("could not create event loop");
        }
        err = gpio_install_isr_service(0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            throw std::runtime_error("could not install GPIO ISR service");
        }
        netif_initialized = true;
    }

    spi_bus_config_t bus_config = {};
    bus_config.mosi_io_num = mosi_pin;
    bus_config.miso_io_num = miso_pin;
    bus_config.sclk_io_num = sclk_pin;
    bus_config.quadwp_io_num = -1;
    bus_config.quadhd_io_num = -1;
    if (spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO) != ESP_OK) {
        throw std::runtime_error("could not initialize SPI bus for W5500");
    }

    spi_device_interface_config_t spi_devcfg = {};
    spi_devcfg.mode = 0;
    spi_devcfg.clock_speed_hz = 20 * 1000 * 1000;
    spi_devcfg.spics_io_num = cs_pin;
    spi_devcfg.queue_size = 20;

    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, &spi_devcfg);
    w5500_config.int_gpio_num = int_pin;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    if (mac == nullptr) {
        throw std::runtime_error("could not create W5500 MAC");
    }

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.reset_gpio_num = rst_pin;
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
    if (phy == nullptr) {
        mac->del(mac);
        throw std::runtime_error("could not create W5500 PHY");
    }

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    if (esp_eth_driver_install(&eth_config, &this->eth_handle) != ESP_OK) {
        throw std::runtime_error("could not install W5500 driver");
    }

    uint8_t mac_addr[6];
    if (esp_read_mac(mac_addr, ESP_MAC_ETH) != ESP_OK) {
        mac_addr[0] = 0x02;
        mac_addr[1] = 0x00;
        mac_addr[2] = 0x00;
        mac_addr[3] = 0x12;
        mac_addr[4] = 0x34;
        mac_addr[5] = 0x56;
    }
    esp_eth_ioctl(this->eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr);

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    this->netif = esp_netif_new(&netif_cfg);
    if (this->netif == nullptr) {
        throw std::runtime_error("could not create ethernet netif");
    }

    if (esp_netif_attach(this->netif, esp_eth_new_netif_glue(this->eth_handle)) != ESP_OK) {
        throw std::runtime_error("could not attach ethernet glue");
    }

    esp_netif_dhcpc_stop(this->netif);
    esp_netif_ip_info_t ip_info = {};
    ip_info.ip.addr = ipaddr_addr(ip.c_str());
    ip_info.gw.addr = ipaddr_addr(gateway.c_str());
    ip_info.netmask.addr = ipaddr_addr(netmask.c_str());
    if (ip_info.ip.addr == IPADDR_NONE || ip_info.gw.addr == IPADDR_NONE || ip_info.netmask.addr == IPADDR_NONE) {
        throw std::runtime_error("invalid IP, gateway or netmask");
    }
    if (esp_netif_set_ip_info(this->netif, &ip_info) != ESP_OK) {
        throw std::runtime_error("could not set static IP info");
    }

    esp_netif_set_default_netif(this->netif);

    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth_event, this);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_ip_event, this);

    if (esp_eth_start(this->eth_handle) != ESP_OK) {
        throw std::runtime_error("could not start ethernet driver");
    }
}

void Ethernet::step() {
    Module::step();
}

void Ethernet::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "info") {
        Module::expect(arguments, 0);
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(this->netif, &ip_info) != ESP_OK) {
            throw std::runtime_error("could not read IP info");
        }
        uint8_t mac[6] = {};
        esp_eth_ioctl(this->eth_handle, ETH_CMD_G_MAC_ADDR, mac);
        echo("ip=" IPSTR " gw=" IPSTR " mask=" IPSTR " mac=%02x:%02x:%02x:%02x:%02x:%02x",
             IP2STR(&ip_info.ip), IP2STR(&ip_info.gw), IP2STR(&ip_info.netmask),
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        Module::call(method_name, arguments);
    }
}
