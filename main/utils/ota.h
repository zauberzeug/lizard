#pragma once
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string>
#include <vector>

namespace ota {

bool receive_firmware_via_uart();
bool run_uart_bridge_for_device_ota(uart_port_t upstream_port = UART_NUM_0, uart_port_t downstream_port = UART_NUM_1);

void start_ota_bridge_task(uart_port_t upstream_port = UART_NUM_0, uart_port_t downstream_port = UART_NUM_1);
bool is_uart_bridge_running();

// Bridge detection and automatic OTA functions
std::vector<std::string> detect_required_bridges(const std::string& target_name);
std::vector<std::string> build_bridge_path(const std::string& target_name);
bool activate_bridges(const std::vector<std::string>& bridge_path);
bool perform_automatic_ota(const std::string& target_name);

} // namespace ota
