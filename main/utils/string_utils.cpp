#include "string_utils.h"
#include <string>
#include <stdexcept>
#include <stdarg.h>

std::string cut_first_word(std::string &msg, const char delimiter) {
    const int space = msg.find(delimiter);
    const std::string word = space < 0 ? msg : msg.substr(0, space);
    msg = space < 0 ? std::string() : msg.substr(space + 1);
    return word;
}

bool starts_with(const std::string haystack, const std::string needle) {
    return haystack.substr(0, needle.length()) == needle;
}

int csprintf(char* buffer, size_t buffer_len, const char *format, ...) {
    va_list args;

    printf("%i\n", buffer_len);
    int res;
    va_start(args, format);
    res = std::vsnprintf(buffer, buffer_len, format, args);
    va_end(args);

    if(res < 0)
        throw std::runtime_error("encoding error.");
    if(res > buffer_len - 1)
        throw std::runtime_error("buffer too small.");
    return res;
}