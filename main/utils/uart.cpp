#include "uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <cstdarg>
#include <stdexcept>
#include <stdio.h>
#include <string>

static EchoRelayHandler relay_handler = nullptr;
static uint8_t echo_target_id = 0xff; // 0xff = normal echo, else = relay to target
static TaskHandle_t relay_task = nullptr;

// Simple redirect system: when target is set to a valid ID (not 0xff), echo output
// is relayed to that target via the handler. Task-scoped for safety.

void echo_set_relay_handler(EchoRelayHandler handler) {
    relay_handler = handler;
}

void echo_set_target(uint8_t target) {
    echo_target_id = target;
    relay_task = xTaskGetCurrentTaskHandle();
}

void echo(const char *format, ...) {
    static char buffer[1024];

    va_list args;
    va_start(args, format);
    const int num_chars = std::vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    int pos = std::min(num_chars, static_cast<int>(sizeof(buffer) - 2));
    va_end(args);

    pos += std::sprintf(&buffer[pos], "\n");

    uint8_t checksum = 0;
    int start = 0;
    for (unsigned int i = 0; i < pos; ++i) {
        if (buffer[i] == '\n') {
            buffer[i] = '\0';
            printf("%s@%02x\n", &buffer[start], checksum);
            if (echo_target_id != 0xff && relay_handler && relay_task == xTaskGetCurrentTaskHandle()) {
                relay_handler(echo_target_id, &buffer[start]);
            }
            start = i + 1;
            checksum = 0;
        } else {
            checksum ^= buffer[i];
        }
    }
}

int strip(char *buffer, int len) {
    while (len > 0 &&
           (buffer[len - 1] == ' ' ||
            buffer[len - 1] == '\t' ||
            buffer[len - 1] == '\r' ||
            buffer[len - 1] == '\n')) {
        len--;
    }
    buffer[len] = 0;
    return len;
}

int check(char *buffer, int len) {
    len = strip(buffer, len);
    if (len >= 3 && buffer[len - 3] == '@') {
        uint8_t checksum = 0;
        for (int i = 0; i < len - 3; ++i) {
            checksum ^= buffer[i];
        }
        const std::string hex_number(&buffer[len - 2], 2);
        if (std::stoi(hex_number, 0, 16) != checksum) {
            throw std::runtime_error("checksum mismatch");
        }
        len -= 3;
    }
    buffer[len] = 0;
    return len;
}
