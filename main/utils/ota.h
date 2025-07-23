#pragma once
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string>

namespace ota {

bool receive_firmware_via_uart();
bool run_uart_bridge_for_device_ota(uart_port_t upstream_port = UART_NUM_0, uart_port_t downstream_port = UART_NUM_1);

void start_ota_bridge_task(uart_port_t upstream_port = UART_NUM_0, uart_port_t downstream_port = UART_NUM_1);
bool is_uart_bridge_running();

} // namespace ota
