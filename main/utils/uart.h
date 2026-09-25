#pragma once

#include <cstdint>
#include <functional>
#include <vector>

constexpr int CONSOLE_LINE_SIZE = 2048;                     // longest line on the wire in either direction, line ending included
constexpr int CONSOLE_PAYLOAD_SIZE = CONSOLE_LINE_SIZE - 5; // what a line may carry before "@xx\r\n" (stdout ends lines with CRLF)

void echo(const char *fmt, ...);
typedef std::function<void(const char *line)> EchoCallback;
int register_echo_callback(const EchoCallback &callback);
void unregister_echo_callback(const int handle);
int strip(char *buffer, int len);
int check(char *buffer, int len, bool *checksum_ok);
