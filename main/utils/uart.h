#pragma once

#include <cstdint>
#include <functional>
#include <vector>

void echo(const char *fmt, ...);
typedef std::function<void(const char *line)> EchoCallback;
void register_echo_callback(const EchoCallback &callback);
int strip(char *buffer, int len);
int check(char *buffer, int len, bool *checksum_ok);
