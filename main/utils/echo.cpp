#include "echo.h"
#include <cstdarg>
#include <stdio.h>
#include <string>

void echo(const char *format, ...) {
    static char buffer[1024];
    int pos = 0;

    va_list args;
    va_start(args, format);
    pos += std::vsnprintf(&buffer[pos], sizeof buffer - pos - 1, format, args);
    va_end(args);

    pos += std::sprintf(&buffer[pos], "\n");

    uint8_t checksum = 0;
    int start = 0;
    for (unsigned int i = 0; i < pos; ++i) {
        if (buffer[i] == '\n') {
            buffer[i] = '\0';
            printf("%s@%02x\n", &buffer[start], checksum);
            start = i + 1;
            checksum = 0;
        } else {
            checksum ^= buffer[i];
        }
    }
}
