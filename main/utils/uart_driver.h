#pragma once

#include "driver/uart.h"
#include "esp_err.h"

// Bytes in the 128-byte hardware FIFO before the receive interrupt fires (IDF default: 120).
// At 921600 baud the default leaves ~70 us for the ISR, and a late one silently loses bytes.
constexpr int RX_FULL_THRESHOLD_BYTES = 32;

// Installs a UART driver from core 1 so its interrupt is allocated there, away from the Bluetooth controller
// on core 0 (esp_intr_alloc binds an interrupt to the calling core), and lowers the receive FIFO threshold.
esp_err_t install_uart_driver_on_core1(uart_port_t port, int rx_buffer_size, int tx_buffer_size);
