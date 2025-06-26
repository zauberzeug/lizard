#pragma once
#include <string>

namespace ota {

typedef struct {
    size_t total_size;
    size_t received_size;
    bool in_progress;
} uart_ota_status_t;

bool uart_ota_start();
bool uart_ota_receive_firmware();
void uart_ota_abort();
uart_ota_status_t uart_ota_get_status();

} // namespace ota
