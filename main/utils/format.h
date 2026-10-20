#pragma once

#include "../compilation/expression.h"
#include <string>
#include <vector>

// Printf-style formatter for Lizard expressions.
//
// Walks `fmt` and replaces each specifier with the corresponding argument, starting at `arguments[args_start]`.
// Flags, width, and precision between '%' and the specifier letter are passed straight to snprintf,
// so the full printf syntax works (e.g. "%.3f", "%-10s", "%5d").
// Recognized specifiers:
//   %d  — integer
//   %f  — number (integer is promoted)
//   %s  — string (bool is rendered as "true"/"false")
//   %%  — literal %
//
// Throws on an unterminated '%' or too few arguments.
// Type mismatches fall through to snprintf and may produce garbage output rather than a clean error;
// extra trailing arguments are ignored silently.
std::string format_args(const std::string &fmt,
                        const std::vector<ConstExpression_ptr> &arguments,
                        size_t args_start = 0);
