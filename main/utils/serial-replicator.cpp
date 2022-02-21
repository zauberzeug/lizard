#include "serial-replicator.h"

#include <cstddef>
#include <numeric>
#include <vector>

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_err.h>
#include <esp_flash_partitions.h>
#include <esp_log.h>
#include <esp_spi_flash.h>

#include <esp32_port.h>
#include <esp_loader.h>

namespace ZZ::Replicator {

static constexpr char TAG[]{"replicator"};

const uint32_t localUartPort{UART_NUM_1};
const gpio_num_t remoteEnable{GPIO_NUM_14};
const gpio_num_t remoteGpio0{GPIO_NUM_25};
const gpio_num_t remoteTxPin{GPIO_NUM_26};
const gpio_num_t remoteRxPin{GPIO_NUM_27};
const uint32_t baseBaudrate{115200};
const uint32_t transferBlockSize{0x1000};

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

static auto initConnection() -> bool {
    loader_esp32_config_t conf{};
    conf.baud_rate = baseBaudrate;
    conf.uart_port = localUartPort;
    conf.uart_rx_pin = remoteTxPin;
    conf.uart_tx_pin = remoteRxPin;
    conf.reset_trigger_pin = remoteEnable;
    conf.gpio0_trigger_pin = remoteGpio0;

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

static auto upBaudrate() -> bool {
    const uint32_t higherRate{baseBaudrate * 8};
    esp_loader_error_t status{esp_loader_change_baudrate(higherRate)};

    ESP_LOGD(TAG, "esp_loader_change_baudrate(%u)", higherRate);
    HANDLE_ERROR(status, "raising target baudrate");

    esp_err_t ec{uart_set_baudrate(UART_NUM_1, higherRate)};

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

/* parses the partition table to determine extents of image to flash.
 * based on github.com/espressif/esp-idf/blob/v4.4/components/spi_flash/partition.c#L164 */
static auto getUsedFlashSize(uint32_t &usedSize) -> esp_err_t {
    const void *ptr;
    spi_flash_mmap_handle_t handle;
    esp_err_t ec;

    /* map page 0 */
    ec = spi_flash_mmap(CONFIG_PARTITION_TABLE_OFFSET & 0xFFFF0000, SPI_FLASH_SEC_SIZE, SPI_FLASH_MMAP_DATA, &ptr, &handle);
    Unmapper unmapper{handle};

    if (ec != ESP_OK) {
        return ec;
    }

    /* adjust previously page-aligned pointer back to beginning of partition table */
    auto bytePtr{reinterpret_cast<const std::byte *>(ptr)};
    bytePtr += CONFIG_PARTITION_TABLE_OFFSET & 0xFFFF;

    auto partitionPtr{reinterpret_cast<const esp_partition_info_t *>(bytePtr)};
    usedSize = 0;

    /* walk all parition entries */
    for (; partitionPtr->magic == ESP_PARTITION_MAGIC; ++partitionPtr) {
        ESP_LOGD(TAG, "Visiting partition: [%*.s] [%X/%X]", sizeof(partitionPtr->label),
                 partitionPtr->label, partitionPtr->pos.offset, partitionPtr->pos.size);

        usedSize = partitionPtr->pos.offset + partitionPtr->pos.size;
    }

    return ESP_OK;
}

static auto neededBlocks(const uint32_t value, const uint32_t blockSize) -> uint32_t {
    uint32_t blockCount{value / blockSize};

    if (value % blockSize > 0) {
        ++blockCount;
    }

    return blockCount;
}

static auto flash(uint32_t usedSize) -> bool {
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
    while (toSend >= transferBlockSize) {
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

auto flashReplica() -> bool {
    ESP_LOGI(TAG, "Initializing pins..");
    if (!initConnection()) {
        return false;
    }

    Deiniter deiniter{};

    ESP_LOGI(TAG, "Connecting..");
    if (!connect()) {
        return false;
    }

    ESP_LOGI(TAG, "Raising baudrate..");
    if (!upBaudrate()) {
        return false;
    }

    uint32_t usedSize;
    esp_err_t ec{getUsedFlashSize(usedSize)};

    HANDLE_ESP_ERROR(ec, "querying used flash size");

    if (!flash(usedSize)) {
        return false;
    }

    ESP_LOGI(TAG, "Replica complete.");

    return true;
}

} // namespace ZZ::Replicator
