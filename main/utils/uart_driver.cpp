#include "uart_driver.h"
#include "esp_ipc.h"
#include <algorithm>

namespace {

struct InstallRequest {
    uart_port_t port;
    int rx_buffer_size;
    int tx_buffer_size;
    esp_err_t result;
};

void install(void *arg) {
    auto *request = static_cast<InstallRequest *>(arg);
    request->result = uart_driver_install(request->port, request->rx_buffer_size, request->tx_buffer_size,
                                          RX_EVENT_QUEUE_SIZE, nullptr, 0);
}

} // namespace

esp_err_t install_uart_driver_on_core1(uart_port_t port, int rx_buffer_size, int tx_buffer_size) {
    InstallRequest request{port, rx_buffer_size, tx_buffer_size, ESP_FAIL};
    const esp_err_t ipc_result = esp_ipc_call_blocking(1, install, &request);
    if (ipc_result != ESP_OK) {
        return ipc_result;
    }
    if (request.result != ESP_OK) {
        return request.result;
    }
    return uart_set_rx_full_threshold(port, RX_FULL_THRESHOLD_BYTES);
}

void discard_uart_input(uart_port_t port, int count) {
    uint8_t scratch[128];
    while (count > 0) {
        const int read = uart_read_bytes(port, scratch, std::min<int>(count, sizeof(scratch)), 0);
        if (read <= 0) {
            break;
        }
        count -= read;
    }
}
