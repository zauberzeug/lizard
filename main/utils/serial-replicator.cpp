#include "serial-replicator.h"

#include <cstddef>
#include <numeric>
#include <vector>

#include <esp_err.h>
#include <esp_flash_partitions.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_spi_flash.h>

#include <esp32_port.h>
#include <esp_loader.h>
#include <esp_partition.h>

namespace ZZ::Replicator {

static constexpr char TAG[]{"replicator"};

static constexpr char const *errorStrings[]{
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

#define HANDLE_ERROR(status, segment)                                           \
    do {                                                                        \
        if (status != ESP_LOADER_SUCCESS) {                                     \
            ESP_LOGE(TAG, "Error while " segment ": %s", errorStrings[status]); \
            return false;                                                       \
        }                                                                       \
    } while (false)

#define HANDLE_ESP_ERROR(ec, segment)                                          \
    do {                                                                       \
        if (ec != ESP_OK) {                                                    \
            ESP_LOGE(TAG, "Error while " segment ": %s", esp_err_to_name(ec)); \
            return false;                                                      \
        }                                                                      \
    } while (false)

static auto initConnection(const uart_port_t uart_num,
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

static auto upBaudrate(uart_port_t uart_num, uint32_t base_baud_rate) -> bool {
    const uint32_t higherRate{base_baud_rate * 8};
    esp_loader_error_t status{esp_loader_change_baudrate(higherRate)};

    ESP_LOGD(TAG, "esp_loader_change_baudrate(%u)", higherRate);
    HANDLE_ERROR(status, "raising target baudrate");

    esp_err_t ec{uart_set_baudrate(uart_num, higherRate)};

    HANDLE_ESP_ERROR(ec, "raising host baudrate");

    return true;
}

class Unmapper {
    spi_flash_mmap_handle_t m_handle;

public:
    Unmapper(spi_flash_mmap_handle_t handle) : m_handle(handle) {}

    ~Unmapper() {
        spi_flash_munmap(m_handle);
    }
};

static auto neededBlocks(const uint32_t value, const uint32_t blockSize) -> uint32_t {
    uint32_t blockCount{value / blockSize};

    if (value % blockSize > 0) {
        ++blockCount;
    }

    return blockCount;
}

static auto flash(uint32_t usedSize, uint32_t transferBlockSize) -> bool {
    const uint32_t pageCount{neededBlocks(usedSize, SPI_FLASH_MMU_PAGE_SIZE)};
    const uint32_t blockCount{neededBlocks(usedSize, transferBlockSize)};

    ESP_LOGI(TAG, "Replicating [%u] bytes, from [%u] pages, in [%u] blocks", usedSize, pageCount, blockCount);

    /* Fill vector with ascending indices starting at 0 */
    std::vector<int> pageIndices(pageCount);
    std::iota(pageIndices.begin(), pageIndices.end(), 0);

    spi_flash_mmap_handle_t handle;
    const void *ptr;

    ESP_ERROR_CHECK(spi_flash_mmap_pages(pageIndices.data(), pageIndices.size(), SPI_FLASH_MMAP_DATA, &ptr, &handle));
    Unmapper unmapper{handle};

    esp_loader_error_t status;

    status = esp_loader_flash_start(0, usedSize, transferBlockSize);
    HANDLE_ERROR(status, "erasing target flash");

    auto bytePtr{reinterpret_cast<const std::byte *>(ptr)};
    uint32_t toSend = usedSize;

    /* Send all non-partial block */
    int count = 0;
    while (toSend >= transferBlockSize) {
        if ((count++) % 10 == 0) {
            ESP_LOGI(TAG, "%d/%d kb", (usedSize - toSend) / 1000, usedSize / 1000);
        }
        status = esp_loader_flash_write(bytePtr, transferBlockSize);
        ESP_LOGD(TAG, "esp_loader_flash_write(0x%08X)", usedSize - toSend);

        HANDLE_ERROR(status, "writing target flash");

        bytePtr += transferBlockSize;
        toSend -= transferBlockSize;
    }

    if (toSend > 0) {
        /* Send last (partial) block */
        status = esp_loader_flash_write(bytePtr, toSend);

        ESP_LOGD(TAG, "esp_loader_flash_write(0x%08X)", usedSize - toSend);

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

auto flashReplica(const uart_port_t uart_num,
                  const gpio_num_t enable_pin,
                  const gpio_num_t boot_pin,
                  const gpio_num_t rx_pin,
                  const gpio_num_t tx_pin,
                  const uint32_t baud_rate,
                  const uint32_t block_size) -> bool {
    ESP_LOGI(TAG, "Initializing pins..");
    if (!initConnection(uart_num, enable_pin, boot_pin, rx_pin, tx_pin, baud_rate, block_size)) {
        return false;
    }

    Deiniter deiniter{};

    ESP_LOGI(TAG, "Connecting..");
    if (!connect()) {
        return false;
    }

    ESP_LOGI(TAG, "Raising baudrate..");
    if (!upBaudrate(uart_num, baud_rate)) {
        return false;
    }

    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition == nullptr) {
        ESP_LOGE(TAG, "Failed to find OTA partition");
        return false;
    }

    ESP_LOGI(TAG, "Running partition: [%s] address: [%X] size: [%X]",
             running_partition->label, running_partition->address, running_partition->size);

    if (running_partition->size == 0) {
        ESP_LOGE(TAG, "Failed to determine used flash size");
        return false;
    }

    if (!flash(running_partition->size, block_size)) {
        return false;
    }

    ESP_LOGI(TAG, "Replica complete.");

    return true;
}

} // namespace ZZ::Replicator
