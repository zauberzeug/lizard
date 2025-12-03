#pragma once

void echo(const char *fmt, ...);
typedef void (*EchoCallback)(const char *line, void *context);
void echo_push_callback(EchoCallback callback, void *context);
void echo_pop_callback(EchoCallback callback, void *context);
int strip(char *buffer, int len);
int check(char *buffer, int len);
