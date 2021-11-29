#pragma once

enum OutputTarget
{
    uart0 = 1,
    uart1 = 2,
    all = 3,
};

enum OutputType
{
    raw,
    text,
    code,
};

void echo(OutputTarget target, OutputType type, const char *fmt, ...);
