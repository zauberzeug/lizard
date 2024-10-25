#include "string_utils.h"
#include <stdarg.h>
#include <stdexcept>
#include <string>

std::string cut_first_word(std::string &msg, const char delimiter) {
    const int space = msg.find(delimiter);
    const std::string word = space < 0 ? msg : msg.substr(0, space);
    msg = space < 0 ? std::string() : msg.substr(space + 1);
    return word;
}

bool starts_with(const std::string haystack, const std::string needle) {
    return haystack.substr(0, needle.length()) == needle;
}

int csprintf(char *buffer, size_t buffer_len, const char *format, ...) {
    va_list args;

    va_start(args, format);
    const int num_chars = std::vsnprintf(buffer, buffer_len, format, args);
    va_end(args);

    if (num_chars < 0)
        throw std::runtime_error("encoding error");
    if (num_chars > buffer_len - 1)
        throw std::runtime_error("buffer too small");

    return num_chars;
}