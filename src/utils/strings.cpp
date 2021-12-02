#include "strings.h"

#include <string>

std::string cut_first_word(std::string &msg, const char delimiter)
{
    const int space = msg.find(delimiter);
    const std::string word = space < 0 ? msg : msg.substr(0, space);
    msg = space < 0 ? std::string() : msg.substr(space + 1);
    return word;
}

bool starts_with(const std::string haystack, const std::string needle)
{
    return haystack.substr(0, needle.length()) == needle;
}