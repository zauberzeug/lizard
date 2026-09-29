#pragma once

#include "driver/uart.h"
#include "esp_err.h"

// Bytes in the 128-byte hardware FIFO before the receive interrupt fires (IDF default: 120).
// At 921600 baud the default leaves ~70 us for the ISR, and a late one silently loses bytes.
constexpr int RX_FULL_THRESHOLD_BYTES = 32;

// Depth of the driver's event queue. Nothing reads it, it only has to exist: when the receive ring runs full while
// a line end is detected, the receive ISR of IDF 5.3.1 (and still of master) posts to the queue without a NULL
// check, and FreeRTOS asserts in the ISR if there is none (#146, #302). Any depth works; 20 is what #146 chose.
constexpr int RX_EVENT_QUEUE_SIZE = 20;

// Installs a UART driver from core 1 so its interrupt is allocated there, away from the Bluetooth controller
// on core 0 (esp_intr_alloc binds an interrupt to the calling core), lowers the receive FIFO threshold and
// gives the driver an event queue (see RX_EVENT_QUEUE_SIZE).
esp_err_t install_uart_driver_on_core1(uart_port_t port, int rx_buffer_size, int tx_buffer_size);
