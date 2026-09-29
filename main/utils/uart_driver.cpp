#include "uart_driver.h"
#include "esp_ipc.h"

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
