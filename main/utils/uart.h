#pragma once

void echo(const char *fmt, ...);
int strip(char *buffer, int len);
int check(char *buffer, int len, bool *checksum_ok = nullptr);
