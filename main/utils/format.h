#pragma once

#include "../compilation/expression.h"
#include <string>
#include <vector>

// Printf-style formatter for Lizard expressions.
//
// Walks `fmt` and replaces each specifier with the corresponding argument,
// starting at `arguments[args_start]`. Flags, width, and precision between
// '%' and the specifier letter are passed straight to snprintf, so the full
// printf syntax works (e.g. "%.3f", "%-10s", "%5d"). Recognized specifier
// letters:
//   %d  — integer
//   %f  — number (integer is promoted)
//   %s  — string (also accepts bool as "true"/"false")
//   %b  — bool → "true"/"false" (Lizard extension; flags ignored)
//   %%  — literal %
//
// Throws std::runtime_error on type mismatch, unknown specifier, too few
// arguments, or an unterminated % at the end of the format string.
// Extra trailing arguments are ignored silently.
std::string format_args(const std::string &fmt,
                        const std::vector<ConstExpression_ptr> &arguments,
                        size_t args_start = 0);
