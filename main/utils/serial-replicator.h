#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"

namespace ZZ::Replicator {

/* Clones the current flash image, from address 0 up to the end of the ota_0
 * partition, onto the target connected via UART1. The NVS partition is not
 * copied but written as 0xFF, so the target boots with an empty startup script
 * and default settings. The otadata partition is written as 0xFF too, so the
 * target boots ota_0, and ota_0 receives the running app, also when this core
 * runs from ota_1. Returns true on success.
 * On failure, returns false and prints a message detailing what went wrong
 * to the error log. */
auto flash_replica(const uart_port_t uart_num,
                   const gpio_num_t enable_pin,
                   const gpio_num_t boot_pin,
                   const gpio_num_t rx_pin,
                   const gpio_num_t tx_pin,
                   const uint32_t baud_rate,
                   const uint32_t block_size = 0x1000) -> bool;

} // namespace ZZ::Replicator
