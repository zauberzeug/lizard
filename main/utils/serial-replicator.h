#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"

namespace ZZ::Replicator {

/* Clones the current flash image, up until the end of the last partition,
 * onto the target connected via UART1. Returns true on success.
 * On failure, returns false and prints a message detailing what went wrong
 * to the error log. */
auto flashReplica(const uart_port_t uart_num,
                  const gpio_num_t enable_pin,
                  const gpio_num_t boot_pin,
                  const gpio_num_t rx_pin,
                  const gpio_num_t tx_pin,
                  const uint32_t baud_rate,
                  const uint32_t block_size = 0x1000) -> bool;

} // namespace ZZ::Replicator
