#pragma once

enum OutputTarget {
    up = 1,
    down = 2,
    both = 3,
};

enum OutputType {
    raw,
    text,
    code,
};

void echo(const OutputTarget target, const OutputType type, const char *fmt, ...);
