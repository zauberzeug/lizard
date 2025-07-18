#pragma once
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace ota {

bool uart_ota_receive_firmware();
bool uart_ota_bridge();

// New task-based functions
void start_ota_bridge_task();
void stop_ota_bridge_task();
bool is_ota_bridge_running();

} // namespace ota
