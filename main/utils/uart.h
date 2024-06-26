#pragma once

void echo(const char *fmt, ...);
int strip(char *buffer, int len);
int append_checksum(char *buffer, int len, int maxlen);
int check(char *buffer, int len);
