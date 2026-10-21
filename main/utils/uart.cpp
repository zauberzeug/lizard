#include "uart.h"
#include "esp_timer.h"
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <stdexcept>
#include <stdio.h>
#include <string>

static std::vector<std::pair<int, EchoCallback>> echo_callbacks;

// printf() returns once the bytes are on the wire, so a console too slow for its output stretches the loop (#258)
static constexpr int64_t CONSOLE_WINDOW_US = 1000000;
static constexpr int CONSOLE_STRETCHED_WINDOWS = 3;
static constexpr int64_t CONSOLE_WARNING_GAP_US = 60 * CONSOLE_WINDOW_US;

static int64_t console_blocked_us = 0; // spent in printf() since the last check_console_load()

void check_console_load(const int64_t work_us, const int64_t period_us) {
    static int64_t window_start_us = esp_timer_get_time();
    static int cycles = 0;
    static int stretched_cycles = 0;
    static int stretched_windows = 0;
    static int64_t last_warning_us = -CONSOLE_WARNING_GAP_US;

    const int64_t blocked_us = console_blocked_us;
    console_blocked_us = 0;
    ++cycles;
    // stretched: the cycle ran 20 % past its period, printing took half of it, and without printing it would have fit
    if (work_us > period_us + period_us / 5 && blocked_us * 2 >= work_us && work_us - blocked_us <= period_us) {
        ++stretched_cycles;
    }
    const int64_t now_us = esp_timer_get_time();
    const int64_t window_us = now_us - window_start_us;
    if (window_us < CONSOLE_WINDOW_US) {
        return;
    }
    stretched_windows = stretched_cycles * 2 >= cycles ? stretched_windows + 1 : 0;
    const int loop_ms = window_us / cycles / 1000;
    window_start_us = now_us;
    cycles = 0;
    stretched_cycles = 0;
    if (stretched_windows >= CONSOLE_STRETCHED_WINDOWS && now_us - last_warning_us >= CONSOLE_WARNING_GAP_US) {
        last_warning_us = now_us;
        echo("warning: printing stretched the loop to %d ms, the console baud rate is too low for its output", loop_ms);
    }
}

int register_echo_callback(const EchoCallback &callback) {
    static int next_handle = 0;
    echo_callbacks.emplace_back(++next_handle, callback);
    return next_handle;
}

void unregister_echo_callback(const int handle) {
    echo_callbacks.erase(
        std::remove_if(echo_callbacks.begin(), echo_callbacks.end(), [handle](const auto &entry) { return entry.first == handle; }),
        echo_callbacks.end());
}

void echo(const char *format, ...) {
    static char buffer[CONSOLE_PAYLOAD_SIZE + 2]; // payload, newline, terminator

    va_list args;
    va_start(args, format);
    int pos = std::vsnprintf(buffer, CONSOLE_PAYLOAD_SIZE + 1, format, args);
    va_end(args);
    if (pos < 0) {
        return;
    }
    if (pos > CONSOLE_PAYLOAD_SIZE) {
        // a truncated line would still carry a valid checksum, so report the loss instead of the line
        pos = std::snprintf(buffer, CONSOLE_PAYLOAD_SIZE + 1, "warning: console line of %d bytes exceeds %d bytes and was dropped",
                            pos + 5, CONSOLE_LINE_SIZE);
    }

    buffer[pos++] = '\n';
    buffer[pos] = '\0';

    uint8_t checksum = 0;
    int start = 0;
    for (unsigned int i = 0; i < pos; ++i) {
        if (buffer[i] == '\n') {
            buffer[i] = '\0';
            const int64_t before_us = esp_timer_get_time();
            printf("%s@%02x\n", &buffer[start], checksum);
            console_blocked_us += esp_timer_get_time() - before_us;
            for (const auto &[handle, callback] : echo_callbacks) {
                callback(&buffer[start]);
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

int check(char *buffer, int len, bool *checksum_ok) {
    len = strip(buffer, len);
    bool ok = true;
    if (len >= 3 && buffer[len - 3] == '@') {
        uint8_t checksum = 0;
        for (int i = 0; i < len - 3; ++i) {
            checksum ^= buffer[i];
        }
        const std::string hex_number(&buffer[len - 2], 2);
        try {
            if (std::stoi(hex_number, 0, 16) != checksum) {
                ok = false;
            } else {
                len -= 3;
            }
        } catch (...) {
            ok = false;
        }
    }
    buffer[len] = 0;
    *checksum_ok = ok;
    return len;
}
