#include "boot_guard.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "timing.h"
#include "uart.h"
#include <cstdio>
#include <cstring>

namespace boot_guard {

static constexpr unsigned MAX_FAILED_BOOTS = 2;
static constexpr unsigned long SETTLE_MS = 5000;
static constexpr uint32_t MAGIC = 0x4c495a44;

// RTC memory survives panics, watchdogs and esp_restart(), but not a power-on or EN reset.
RTC_NOINIT_ATTR static uint32_t magic;
RTC_NOINIT_ATTR static uint32_t failed_boots;
RTC_NOINIT_ATTR static bool armed;
RTC_NOINIT_ATTR static char reason[128];

static const char *reset_reason_name(const esp_reset_reason_t reason) {
    switch (reason) {
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
        return "task watchdog";
    case ESP_RST_WDT:
        return "watchdog";
    case ESP_RST_BROWNOUT:
        return "brownout";
    default:
        return nullptr;
    }
}

bool should_run_startup() {
    const esp_reset_reason_t reset_reason = esp_reset_reason();
    if (magic != MAGIC || reset_reason == ESP_RST_POWERON || reset_reason == ESP_RST_EXT || reset_reason == ESP_RST_UNKNOWN) {
        magic = MAGIC;
        failed_boots = 0;
        armed = false;
        reason[0] = '\0';
    }
    // an armed guard means the last boot died before it settled; a deliberate esp_restart() is not a failure
    if (armed) {
        if (const char *name = reset_reason_name(reset_reason)) {
            failed_boots++;
            std::snprintf(reason, sizeof(reason), "%s", name);
        }
    }
    if (failed_boots >= MAX_FAILED_BOOTS) {
        echo("error: startup skipped after %u failed boots (%s)", failed_boots, reason);
        failed_boots = 0;
        armed = false;
        return false;
    }
    armed = true;
    return true;
}

void startup_failed(const char *what) {
    failed_boots++;
    std::snprintf(reason, sizeof(reason), "%s", what);
    echo("error while loading startup script: %s", what);
    echo("restarting (failed boot %u of %u)", failed_boots, MAX_FAILED_BOOTS);
    uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(500));
    esp_restart();
}

void step() {
    static const unsigned long start = millis();
    if (armed && millis_since(start) > SETTLE_MS) {
        armed = false;
        failed_boots = 0;
        reason[0] = '\0';
    }
}

} // namespace boot_guard
