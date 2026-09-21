#pragma once

#include <cstdint>
#include <functional>
#include <vector>

constexpr int CONSOLE_LINE_SIZE = 2048;                     // longest line on the wire in either direction, including "@xx\n"
constexpr int CONSOLE_PAYLOAD_SIZE = CONSOLE_LINE_SIZE - 4; // what a line may carry before its checksum and newline

void echo(const char *fmt, ...);
typedef std::function<void(const char *line)> EchoCallback;
void register_echo_callback(const EchoCallback &callback);
int strip(char *buffer, int len);
int check(char *buffer, int len, bool *checksum_ok);
