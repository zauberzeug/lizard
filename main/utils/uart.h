#pragma once

#include <cstdint>
#include <functional>
#include <vector>

void echo(const char *fmt, ...);
typedef std::function<void(const char *line)> EchoCallback;
void echo_register_callback(EchoCallback handler);
int strip(char *buffer, int len);
int check(char *buffer, int len);
