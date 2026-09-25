#include "serial-replicator.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include <esp_err.h>
#include <esp_flash.h>
#include <esp_log.h>
#include <esp_ota_ops.h>

#include <esp32_port.h>
#include <esp_loader.h>
#include <esp_partition.h>

namespace ZZ::Replicator {

static constexpr char TAG[]{"replicator"};

static constexpr char const *ERROR_STRINGS[]{
    "SUCCESS",          /*!< Success */
    "FAIL",             /*!< Unspecified error */
    "TIMEOUT",          /*!< Timeout elapsed */
    "IMAGE_SIZE",       /*!< Image size to flash is larger than flash size */
    "INVALID_MD5",      /*!< Computed and received MD5 does not match */
    "INVALID_PARAM",    /*!< Invalid parameter passed to function */
    "INVALID_TARGET",   /*!< Connected target is invalid */
    "UNSUPPORTED_CHIP", /*!< Attached chip is not supported */
    "UNSUPPORTED_FUNC", /*!< Function is not supported on attached target */
    "INVALID_RESPONSE", /*!< Internal error */
};

#define HANDLE_ERROR(status, segment)                                            \
    do {                                                                         \
        if (status != ESP_LOADER_SUCCESS) {                                      \
            ESP_LOGE(TAG, "Error while " segment ": %s", ERROR_STRINGS[status]); \
            return false;                                                        \
        }                                                                        \
    } while (false)

#define HANDLE_ESP_ERROR(ec, segment)                                          \
    do {                                                                       \
        if (ec != ESP_OK) {                                                    \
            ESP_LOGE(TAG, "Error while " segment ": %s", esp_err_to_name(ec)); \
            return false;                                                      \
        }                                                                      \
    } while (false)

static auto init_connection(const uart_port_t uart_num,
                            const gpio_num_t enable_pin,
                            const gpio_num_t boot_pin,
                            const gpio_num_t rx_pin,
                            const gpio_num_t tx_pin,
                            const uint32_t baud_rate,
                            const uint32_t block_size) -> bool {
    loader_esp32_config_t conf{};
    conf.baud_rate = baud_rate;
    conf.uart_port = uart_num;
    conf.uart_rx_pin = rx_pin;
    conf.uart_tx_pin = tx_pin;
    conf.reset_trigger_pin = enable_pin;
    conf.gpio0_trigger_pin = boot_pin;

    esp_loader_error_t status{loader_port_esp32_init(&conf)};
    ESP_LOGD(TAG, "loader_port_esp32_init() -> %u", status);

    HANDLE_ERROR(status, "initializing communication pins");

    return true;
}

static auto connect() -> bool {
    esp_loader_connect_args_t args{};
    args.trials = 4;
    args.sync_timeout = 100;
    esp_loader_error_t status{esp_loader_connect(&args)};
    ESP_LOGD(TAG, "esp_loader_connect() -> %u", status);

    HANDLE_ERROR(status, "connecting");

    return true;
}

static auto up_baudrate(uart_port_t uart_num, uint32_t base_baud_rate) -> bool {
    const uint32_t higher_rate{base_baud_rate * 8};
    esp_loader_error_t status{esp_loader_change_baudrate(higher_rate)};

    ESP_LOGD(TAG, "esp_loader_change_baudrate(%lu)", higher_rate);
    HANDLE_ERROR(status, "raising target baudrate");

    esp_err_t ec{uart_set_baudrate(uart_num, higher_rate)};

    HANDLE_ESP_ERROR(ec, "raising host baudrate");

    return true;
}

static auto contains(const esp_partition_t *partition, const uint32_t offset) -> bool {
    return partition != nullptr && offset >= partition->address && offset < partition->address + partition->size;
}

static auto flash(const esp_partition_t *running_partition, uint32_t transfer_block_size) -> bool {
    esp_loader_error_t status;

    // the target gets a blank NVS instead of a copy of ours, so it boots with an empty startup and default settings
    const esp_partition_t *nvs = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, nullptr);
    if (nvs == nullptr) {
        ESP_LOGW(TAG, "No NVS partition found, the target receives a copy of our NVS");
    }
    // a blank otadata makes the target boot ota_0, which gets our running app also when that runs from ota_1
    const esp_partition_t *otadata = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
    const esp_partition_t *ota_0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    for (const esp_partition_t *partition : {nvs, otadata, ota_0}) {
        if (partition != nullptr && (partition->address % transfer_block_size != 0 || partition->size % transfer_block_size != 0)) {
            ESP_LOGE(TAG, "Partition %s at 0x%08lX with %lu bytes is not aligned to the block size of %lu bytes",
                     partition->label, partition->address, partition->size, transfer_block_size);
            return false;
        }
    }
    std::vector<std::byte> blank(transfer_block_size, std::byte{0xFF});
    std::vector<std::byte> block(transfer_block_size);

    // copy up to the end of ota_0, so the target gets the whole app and not only the running partition's size from address 0
    const uint32_t used_size{ota_0 != nullptr ? ota_0->address + ota_0->size : running_partition->size};
    const uint32_t block_count{(used_size + transfer_block_size - 1) / transfer_block_size};
    ESP_LOGI(TAG, "Replicating [%lu] bytes in [%lu] blocks", used_size, block_count);

    status = esp_loader_flash_start(0, used_size, transfer_block_size);
    HANDLE_ERROR(status, "erasing target flash");

    int count = 0;
    for (uint32_t offset = 0; offset < used_size; offset += transfer_block_size) {
        if ((count++) % 10 == 0) {
            ESP_LOGI(TAG, "%lu/%lu kb", offset / 1000, used_size / 1000);
        }
        const uint32_t size{std::min(transfer_block_size, used_size - offset)};
        std::byte *data = block.data();
        if (contains(nvs, offset) || contains(otadata, offset)) {
            data = blank.data();
        } else if (contains(ota_0, offset)) {
            const esp_err_t ec{esp_partition_read(running_partition, offset - ota_0->address, block.data(), size)};
            HANDLE_ESP_ERROR(ec, "reading the running app");
        } else {
            const esp_err_t ec{esp_flash_read(nullptr, block.data(), offset, size)};
            HANDLE_ESP_ERROR(ec, "reading our flash");
        }
        status = esp_loader_flash_write(data, size);
        ESP_LOGD(TAG, "esp_loader_flash_write(0x%08lX)", offset);

        HANDLE_ERROR(status, "writing target flash");
    }

    status = esp_loader_flash_verify();
    HANDLE_ERROR(status, "verifying md5 checksum");

    status = esp_loader_flash_finish(true);
    HANDLE_ERROR(status, "finishing flash process");

    return true;
}

class Deiniter {
public:
    ~Deiniter() {
        loader_port_esp32_deinit();
    }
};

auto flash_replica(const uart_port_t uart_num,
                   const gpio_num_t enable_pin,
                   const gpio_num_t boot_pin,
                   const gpio_num_t rx_pin,
                   const gpio_num_t tx_pin,
                   const uint32_t baud_rate,
                   const uint32_t block_size) -> bool {
    ESP_LOGI(TAG, "Initializing pins..");
    if (!init_connection(uart_num, enable_pin, boot_pin, rx_pin, tx_pin, baud_rate, block_size)) {
        return false;
    }

    Deiniter deiniter{};

    ESP_LOGI(TAG, "Connecting..");
    if (!connect()) {
        return false;
    }

    ESP_LOGI(TAG, "Raising baudrate..");
    if (!up_baudrate(uart_num, baud_rate)) {
        return false;
    }

    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition == nullptr) {
        ESP_LOGE(TAG, "Failed to find OTA partition");
        return false;
    }

    ESP_LOGI(TAG, "Running partition: [%s] address: [%lu] size: [%lu]",
             running_partition->label, running_partition->address, running_partition->size);

    if (running_partition->size == 0) {
        ESP_LOGE(TAG, "Failed to determine used flash size");
        return false;
    }

    if (!flash(running_partition, block_size)) {
        return false;
    }

    ESP_LOGI(TAG, "Replica complete.");

    return true;
}

} // namespace ZZ::Replicator
