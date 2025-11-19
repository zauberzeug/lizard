#pragma once

void echo(const char *fmt, ...);
typedef void (*EchoConsumer)(const char *line, void *context);
void echo_push_consumer(EchoConsumer consumer, void *context);
void echo_pop_consumer(EchoConsumer consumer, void *context);
int strip(char *buffer, int len);
int check(char *buffer, int len);
