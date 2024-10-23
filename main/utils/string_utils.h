#pragma once

#include <string>

std::string cut_first_word(std::string &msg, char delimiter = ' ');

bool starts_with(const std::string haystack, const std::string needle);

int csprintf(char* buffer, size_t buffer_len, const char *format, ...);