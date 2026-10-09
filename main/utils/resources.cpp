#include "resources.h"
#include "soc/soc_caps.h"
#include "soc/spi_pins.h"
#include "soc/uart_channel.h"
#include <list>
#include <stdexcept>

namespace resources {

namespace {

const std::string *pin_owners[GPIO_NUM_MAX] = {};
const std::string *uart_owners[UART_NUM_MAX] = {};
const std::string *can_owners[SOC_TWAI_CONTROLLER_NUM] = {};
const std::string *adc_owners[SOC_ADC_PERIPH_NUM + 1] = {}; // indexed by the 1-based unit number
std::list<std::string> permanent_owners;                    // a list keeps its strings in place, so the pointers above stay valid

const char *describe(const Kind kind) {
    switch (kind) {
    case Kind::pin:
        return "pin";
    case Kind::uart:
        return "uart";
    case Kind::can:
        return "can controller";
    default:
        return "adc unit";
    }
}

const std::string *&slot(const Kind kind, const int index) {
    switch (kind) {
    case Kind::pin:
        if (index < 0 || index >= GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(index)) {
            throw std::runtime_error("invalid pin " + std::to_string(index));
        }
        return pin_owners[index];
    case Kind::uart:
        if (index < 0 || index >= UART_NUM_MAX) {
            throw std::runtime_error("invalid uart number " + std::to_string(index));
        }
        return uart_owners[index];
    case Kind::can:
        if (index < 0 || index >= SOC_TWAI_CONTROLLER_NUM) {
            throw std::runtime_error("invalid can controller " + std::to_string(index));
        }
        return can_owners[index];
    default:
        if (index < 1 || index > SOC_ADC_PERIPH_NUM) {
            throw std::runtime_error("invalid adc unit " + std::to_string(index));
        }
        return adc_owners[index];
    }
}

void take(const Kind kind, const int index, const std::string &owner) {
    const std::string *&current = slot(kind, index);
    if (current) {
        throw std::runtime_error(std::string(describe(kind)) + " " + std::to_string(index) + " is already used by " + *current);
    }
    current = &owner;
}

} // namespace

void reserve(const Kind kind, const int index, const std::string &owner) {
    permanent_owners.push_back(owner);
    take(kind, index, permanent_owners.back());
}

void reserve_boot_resources() {
    reserve(Kind::uart, UART_NUM_0, "the console");
    reserve(Kind::pin, UART_NUM_0_TXD_DIRECT_GPIO_NUM, "the console");
    reserve(Kind::pin, UART_NUM_0_RXD_DIRECT_GPIO_NUM, "the console");
    for (const int pin : {SPI_IOMUX_PIN_NUM_CLK, SPI_IOMUX_PIN_NUM_CS, SPI_IOMUX_PIN_NUM_MISO,
                          SPI_IOMUX_PIN_NUM_MOSI, SPI_IOMUX_PIN_NUM_WP, SPI_IOMUX_PIN_NUM_HD}) {
        reserve(Kind::pin, pin, "the SPI flash");
    }
}

Claims::Claims(const std::string &owner) : owner(owner) {
}

Claims::~Claims() {
    for (const auto &[kind, index] : this->claimed) {
        slot(kind, index) = nullptr;
    }
}

void Claims::claim(const Kind kind, const int index) {
    this->claimed.reserve(this->claimed.size() + 1); // so the record cannot fail after the slot is taken
    take(kind, index, this->owner);
    this->claimed.emplace_back(kind, index);
}

void Claims::pin(const gpio_num_t pin) {
    this->claim(Kind::pin, pin);
}

void Claims::uart(const uart_port_t port) {
    this->claim(Kind::uart, port);
}

void Claims::can() {
    this->claim(Kind::can, 0);
}

void Claims::adc(const int unit_id) {
    this->claim(Kind::adc, unit_id);
}

} // namespace resources
