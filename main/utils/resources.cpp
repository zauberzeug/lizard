#include "resources.h"
#include "hal/ledc_types.h"
#include "soc/soc_caps.h"
#include "soc/spi_pins.h"
#include "soc/uart_channel.h"
#include <algorithm>
#include <stdexcept>

namespace resources {

namespace {

const Claims *pin_owners[GPIO_NUM_MAX] = {};
const Claims *uart_owners[UART_NUM_MAX] = {};
const Claims *can_owners[SOC_TWAI_CONTROLLER_NUM] = {};
const Claims *adc_owners[SOC_ADC_PERIPH_NUM + 1] = {}; // indexed by the 1-based unit number
const Claims *ledc_channel_owners[LEDC_CHANNEL_MAX] = {};
std::vector<const Claims *> ledc_timer_owners[LEDC_TIMER_MAX]; // more than one only while they share the timer
bool ledc_timer_shared[LEDC_TIMER_MAX] = {};

const char *describe(const Kind kind) {
    switch (kind) {
    case Kind::pin:
        return "pin";
    case Kind::uart:
        return "uart";
    case Kind::can:
        return "can controller";
    case Kind::adc:
        return "adc unit";
    case Kind::ledc_channel:
        return "LEDC channel";
    default:
        return "LEDC timer";
    }
}

bool is_ledc_timer(const Kind kind) {
    return kind == Kind::ledc_timer || kind == Kind::shared_ledc_timer;
}

const Claims *&slot(const Kind kind, const int index) {
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
    case Kind::adc:
        if (index < 1 || index > SOC_ADC_PERIPH_NUM) {
            throw std::runtime_error("invalid adc unit " + std::to_string(index));
        }
        return adc_owners[index];
    default:
        if (index < 0 || index >= LEDC_CHANNEL_MAX) {
            throw std::runtime_error("invalid LEDC channel " + std::to_string(index));
        }
        return ledc_channel_owners[index];
    }
}

void take_ledc_timer(const int index, const Claims &owner, const bool shared) {
    if (index < 0 || index >= LEDC_TIMER_MAX) {
        throw std::runtime_error("invalid LEDC timer " + std::to_string(index));
    }
    std::vector<const Claims *> &owners = ledc_timer_owners[index];
    if (!owners.empty() && !(shared && ledc_timer_shared[index])) {
        throw std::runtime_error("LEDC timer " + std::to_string(index) + " is already used by " + owners.front()->owner());
    }
    owners.push_back(&owner);
    ledc_timer_shared[index] = shared;
}

void take(const Kind kind, const int index, const Claims &owner) {
    if (is_ledc_timer(kind)) {
        take_ledc_timer(index, owner, kind == Kind::shared_ledc_timer);
        return;
    }
    const Claims *&current = slot(kind, index);
    if (current) {
        throw std::runtime_error(std::string(describe(kind)) + " " + std::to_string(index) + " is already used by " + current->owner());
    }
    current = &owner;
}

} // namespace

void reserve_boot_resources() {
    static const std::string console_name = "the console";
    static const std::string flash_name = "the SPI flash";
    static Claims console(console_name, false);
    static Claims flash(flash_name, false);
    console.uart(UART_NUM_0);
    console.pin(static_cast<gpio_num_t>(UART_NUM_0_TXD_DIRECT_GPIO_NUM));
    console.pin(static_cast<gpio_num_t>(UART_NUM_0_RXD_DIRECT_GPIO_NUM));
    for (const int pin : {SPI_IOMUX_PIN_NUM_CLK, SPI_IOMUX_PIN_NUM_CS, SPI_IOMUX_PIN_NUM_MISO,
                          SPI_IOMUX_PIN_NUM_MOSI, SPI_IOMUX_PIN_NUM_WP, SPI_IOMUX_PIN_NUM_HD}) {
        flash.pin(static_cast<gpio_num_t>(pin));
    }
}

Claims::Claims(const std::string &name, const bool is_module) : name(name), is_module(is_module) {
}

std::string Claims::owner() const {
    return this->is_module ? "module \"" + this->name + "\"" : this->name;
}

Claims::~Claims() {
    for (const auto &[kind, index] : this->claimed) {
        if (is_ledc_timer(kind)) {
            std::vector<const Claims *> &owners = ledc_timer_owners[index];
            owners.erase(std::remove(owners.begin(), owners.end(), this), owners.end());
        } else {
            slot(kind, index) = nullptr;
        }
    }
}

void Claims::claim(const Kind kind, const int index) {
    this->claimed.reserve(this->claimed.size() + 1); // so the record cannot fail after the slot is taken
    take(kind, index, *this);
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

void Claims::ledc_channel(const int channel) {
    this->claim(Kind::ledc_channel, channel);
}

void Claims::ledc_timer(const int timer) {
    this->claim(Kind::ledc_timer, timer);
}

void Claims::shared_ledc_timer(const int timer) {
    this->claim(Kind::shared_ledc_timer, timer);
}

} // namespace resources
