#pragma once

extern bool uart_xon;

void echo(const char *fmt, ...);
int strip(char *buffer, int len);
int check(char *buffer, int len);
