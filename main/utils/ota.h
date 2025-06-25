#pragma once
#include <string>

namespace ota {

// UART OTA protocol constants
#define UART_OTA_START_MARKER "###OTA_START###"
#define UART_OTA_END_MARKER "###OTA_END###"
#define UART_OTA_READY_MARKER "###OTA_READY###"
#define UART_OTA_CHUNK_SIZE 256  // 256 byte buffer for reading
#define UART_OTA_CHUNK_BYTES 256 // Send 256 byte chunks at a time
#define UART_OTA_TIMEOUT_MS 30000

typedef struct {
    size_t total_size;
    size_t received_size;
    bool in_progress;
} uart_ota_status_t;

// Main UART OTA functions
bool uart_ota_start();
bool uart_ota_receive_firmware();
void uart_ota_abort();
uart_ota_status_t uart_ota_get_status();

// UART configuration functions
bool uart_configure_for_ota();
bool uart_restore_normal_config();

// Task functions
void uart_ota_task(void *pvParameters);
void verify_task(void *pvParameters);

// Utility functions
void verify();
bool version_checker();

// Global flag to check if UART OTA is active
extern bool uart_ota_active;

} // namespace ota
