#include "uart.h"
#include <algorithm>
#include <cstdarg>
#include <stdexcept>
#include <stdio.h>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static EchoConsumer current_consumer = nullptr;
static void *consumer_context = nullptr;
static TaskHandle_t consumer_task = nullptr;

void echo_push_consumer(EchoConsumer consumer, void *context) {
    current_consumer = consumer;
    consumer_context = context;
    consumer_task = xTaskGetCurrentTaskHandle();
}

void echo_pop_consumer(EchoConsumer consumer, void *context) {
    if (current_consumer == consumer && consumer_context == context && consumer_task == xTaskGetCurrentTaskHandle()) {
        current_consumer = nullptr;
        consumer_context = nullptr;
        consumer_task = nullptr;
    }
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
            if (current_consumer && consumer_task == xTaskGetCurrentTaskHandle()) {
                current_consumer(&buffer[start], consumer_context);
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
