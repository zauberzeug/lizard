#include "bno08x.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static constexpr TickType_t I2C_TIMEOUT_TICKS = pdMS_TO_TICKS(100);
static constexpr size_t I2C_CHUNK_SIZE = 64;
static constexpr char TAG[] = "Bno08x";

class I2cDevice {
public:
    I2cDevice(i2c_port_t port, uint8_t address) : port(port), address(address) {}

    bool write(const uint8_t *data, size_t len) {
        if (len == 0) {
            return true;
        }
        esp_err_t err = i2c_master_write_to_device(port, address, data, len, I2C_TIMEOUT_TICKS);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2c write failed: %s", esp_err_to_name(err));
            return false;
        }
        return true;
    }

    bool read(uint8_t *data, size_t len) {
        if (len == 0) {
            return true;
        }
        esp_err_t err = i2c_master_read_from_device(port, address, data, len, I2C_TIMEOUT_TICKS);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2c read failed: %s", esp_err_to_name(err));
            return false;
        }
        return true;
    }

    size_t maxBufferSize() const { return I2C_CHUNK_SIZE; }

private:
    i2c_port_t port;
    uint8_t address;
};

namespace {

void delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

Bno08x *active_instance = nullptr;

int i2c_hal_open(sh2_Hal_t *self);
void i2c_hal_close(sh2_Hal_t *self);
int i2c_hal_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len, uint32_t *t_us);
int i2c_hal_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len);
uint32_t hal_get_time_us(sh2_Hal_t *self);
void hal_callback(void *cookie, sh2_AsyncEvent_t *event);
void sensor_handler(void *cookie, sh2_SensorEvent_t *event);

} // namespace

Bno08x::Bno08x(gpio_num_t reset_pin)
    : port(I2C_NUM_0), address(I2C_ADDR_DEFAULT), int_pin(GPIO_NUM_NC),
      reset_pin(reset_pin), pending_value(nullptr), reset_occurred(false) {
    std::memset(&prodIds, 0, sizeof(prodIds));
    std::memset(&hal, 0, sizeof(hal));
}

Bno08x::~Bno08x() {
    if (active_instance == this) {
        active_instance = nullptr;
    }
}

bool Bno08x::begin_i2c(i2c_port_t port, uint8_t i2c_addr, gpio_num_t int_pin, int32_t sensor_id) {
    if (active_instance != nullptr && active_instance != this) {
        ESP_LOGE(TAG, "only one Bno08x instance is supported");
        return false;
    }

    this->port = port;
    this->address = i2c_addr;
    this->int_pin = int_pin;
    this->device = std::make_unique<I2cDevice>(port, i2c_addr);
    active_instance = this;

    if (this->reset_pin != GPIO_NUM_NC) {
        gpio_set_direction(this->reset_pin, GPIO_MODE_OUTPUT);
        gpio_set_level(this->reset_pin, 1);
    }
    if (this->int_pin != GPIO_NUM_NC) {
        gpio_set_direction(this->int_pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(this->int_pin, GPIO_PULLUP_ONLY);
    }

    hal.open = i2c_hal_open;
    hal.close = i2c_hal_close;
    hal.read = i2c_hal_read;
    hal.write = i2c_hal_write;
    hal.getTimeUs = hal_get_time_us;

    return init(sensor_id);
}

bool Bno08x::init(int32_t sensor_id) {
    (void)sensor_id;
    hardwareReset();

    int status = sh2_open(&hal, hal_callback, this);
    if (status != SH2_OK) {
        ESP_LOGE(TAG, "sh2_open failed: %d", status);
        return false;
    }

    std::memset(&prodIds, 0, sizeof(prodIds));
    status = sh2_getProdIds(&prodIds);
    if (status != SH2_OK) {
        ESP_LOGE(TAG, "sh2_getProdIds failed: %d", status);
        return false;
    }

    sh2_setSensorCallback(sensor_handler, this);

    return true;
}

void Bno08x::hardwareReset() {
    if (reset_pin == GPIO_NUM_NC) {
        delay_ms(10);
        return;
    }

    gpio_set_level(reset_pin, 1);
    delay_ms(10);
    gpio_set_level(reset_pin, 0);
    delay_ms(10);
    gpio_set_level(reset_pin, 1);
    delay_ms(10);
}

bool Bno08x::wasReset() {
    bool value = reset_occurred;
    reset_occurred = false;
    return value;
}

bool Bno08x::enableReport(sh2_SensorId_t sensorId, uint32_t interval_us) {
    sh2_SensorConfig_t config{};
    config.changeSensitivityEnabled = false;
    config.wakeupEnabled = false;
    config.changeSensitivityRelative = false;
    config.alwaysOnEnabled = false;
    config.changeSensitivity = 0;
    config.batchInterval_us = 0;
    config.sensorSpecific = 0;
    config.reportInterval_us = interval_us;

    int status = sh2_setSensorConfig(sensorId, &config);
    if (status != SH2_OK) {
        ESP_LOGE(TAG, "sh2_setSensorConfig(%d) failed: %d", sensorId, status);
        return false;
    }
    return true;
}

bool Bno08x::getSensorEvent(sh2_SensorValue_t *value) {
    pending_value = value;
    value->timestamp = 0;

    sh2_service();

    if (value->timestamp == 0 && value->sensorId != SH2_GYRO_INTEGRATED_RV) {
        return false;
    }
    return true;
}

I2cDevice *Bno08x::get_device() const {
    return device.get();
}

// ---------------------------------------------------------------------------
// SH2 HAL implementation (I2C)
// ---------------------------------------------------------------------------

namespace {

int i2c_hal_open(sh2_Hal_t *self) {
    (void)self;
    auto *imu = active_instance;
    I2cDevice *dev = imu ? imu->get_device() : nullptr;
    if (!imu || !dev) {
        return -1;
    }

    const uint8_t softreset_pkt[] = {5, 0, 1, 0, 1};
    bool success = false;
    for (uint8_t attempt = 0; attempt < 5; ++attempt) {
        if (dev->write(softreset_pkt, sizeof(softreset_pkt))) {
            success = true;
            break;
        }
        delay_ms(30);
    }

    if (!success) {
        return -1;
    }

    delay_ms(300);
    return 0;
}

void i2c_hal_close(sh2_Hal_t *self) {
    (void)self;
}

int i2c_hal_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len, uint32_t *t_us) {
    (void)self;
    (void)t_us;
    auto *imu = active_instance;
    I2cDevice *dev = imu ? imu->get_device() : nullptr;
    if (!imu || !dev || len == 0) {
        return 0;
    }

    uint8_t header[4];
    if (!dev->read(header, sizeof(header))) {
        return 0;
    }

    uint16_t packet_size = static_cast<uint16_t>(header[0]) | (static_cast<uint16_t>(header[1]) << 8);
    packet_size &= ~0x8000U;

    if (packet_size == 0 || packet_size > len) {
        return 0;
    }

    uint16_t cargo_remaining = packet_size;
    std::array<uint8_t, I2C_CHUNK_SIZE> chunk{};
    bool first_read = true;

    while (cargo_remaining > 0) {
        size_t read_size;
        if (first_read) {
            read_size = std::min(static_cast<size_t>(cargo_remaining), chunk.size());
        } else {
            read_size = std::min(static_cast<size_t>(cargo_remaining) + 4, chunk.size());
        }

        if (!dev->read(chunk.data(), read_size)) {
            return 0;
        }

        size_t cargo_read_amount;
        if (first_read) {
            cargo_read_amount = read_size;
            std::memcpy(pBuffer, chunk.data(), cargo_read_amount);
            first_read = false;
        } else {
            cargo_read_amount = read_size - 4;
            std::memcpy(pBuffer, chunk.data() + 4, cargo_read_amount);
        }

        pBuffer += cargo_read_amount;
        cargo_remaining -= static_cast<uint16_t>(cargo_read_amount);
    }

    return packet_size;
}

int i2c_hal_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len) {
    (void)self;
    auto *imu = active_instance;
    I2cDevice *dev = imu ? imu->get_device() : nullptr;
    if (!imu || !dev || len == 0) {
        return 0;
    }

    unsigned remaining = len;
    uint8_t *ptr = pBuffer;

    while (remaining > 0) {
        size_t chunk_len = std::min(static_cast<size_t>(remaining), dev->maxBufferSize());
        if (!dev->write(ptr, chunk_len)) {
            return 0;
        }
        ptr += chunk_len;
        remaining -= static_cast<unsigned>(chunk_len);
    }

    return static_cast<int>(len);
}

uint32_t hal_get_time_us(sh2_Hal_t *self) {
    (void)self;
    return static_cast<uint32_t>(esp_timer_get_time() & 0xFFFFFFFFU);
}

void hal_callback(void *cookie, sh2_AsyncEvent_t *event) {
    auto *imu = static_cast<Bno08x *>(cookie);
    if (event && event->eventId == SH2_RESET && imu) {
        imu->reset_occurred = true;
    }
}

void sensor_handler(void *cookie, sh2_SensorEvent_t *event) {
    auto *imu = static_cast<Bno08x *>(cookie);
    if (!imu || !imu->pending_value) {
        return;
    }

    int rc = sh2_decodeSensorEvent(imu->pending_value, event);
    if (rc != SH2_OK) {
        ESP_LOGE(TAG, "sh2_decodeSensorEvent failed: %d", rc);
        imu->pending_value->timestamp = 0;
    }
}

} // namespace
