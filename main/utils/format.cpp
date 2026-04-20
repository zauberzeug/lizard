#include "format.h"

#include "../compilation/type.h"
#include <cstdio>
#include <stdexcept>

// Specifier letters we recognize. Flags / width / precision between '%' and
// the letter are passed through to snprintf untouched, so all of printf's
// knobs (e.g. "%.3f", "%-10s", "%5d", "%x") work for free.
static bool is_specifier_end(char c) {
    switch (c) {
    case 'd':
    case 'f':
    case 's':
    case 'b':
    case '%':
        return true;
    default:
        return false;
    }
}

std::string format_args(const std::string &fmt,
                        const std::vector<ConstExpression_ptr> &arguments,
                        size_t args_start) {
    std::string out;
    out.reserve(fmt.size());
    size_t arg_idx = args_start;
    size_t i = 0;
    char buf[64];
    while (i < fmt.size()) {
        if (fmt[i] != '%') {
            out += fmt[i++];
            continue;
        }
        const size_t start = i++;
        while (i < fmt.size() && !is_specifier_end(fmt[i])) {
            ++i;
        }
        if (i >= fmt.size()) {
            throw std::runtime_error("format: unterminated '%' in format string");
        }
        const char spec = fmt[i++];
        if (spec == '%') {
            out += '%';
            continue;
        }
        const std::string sub = fmt.substr(start, i - start);
        if (arg_idx >= arguments.size()) {
            throw std::runtime_error("format: too few arguments for format string");
        }
        const auto &arg = arguments[arg_idx++];
        switch (spec) {
        case 'd':
            if ((arg->type & integer) == 0) {
                throw std::runtime_error("format: '%d' expects integer argument");
            }
            std::snprintf(buf, sizeof(buf), sub.c_str(), static_cast<int>(arg->evaluate_integer()));
            out += buf;
            break;
        case 'f':
            // integer promotes to number
            if ((arg->type & numbery) == 0) {
                throw std::runtime_error("format: '%f' expects number/integer argument");
            }
            std::snprintf(buf, sizeof(buf), sub.c_str(), arg->evaluate_number());
            out += buf;
            break;
        case 's': {
            std::string s;
            if ((arg->type & string) != 0) {
                s = arg->evaluate_string();
            } else if ((arg->type & boolean) != 0) {
                s = arg->evaluate_boolean() ? "true" : "false";
            } else {
                throw std::runtime_error("format: '%s' expects string or boolean argument");
            }
            std::snprintf(buf, sizeof(buf), sub.c_str(), s.c_str());
            out += buf;
            break;
        }
        case 'b':
            // printf has no %b; handle ourselves and ignore flags/width
            if ((arg->type & boolean) == 0) {
                throw std::runtime_error("format: '%b' expects boolean argument");
            }
            out += arg->evaluate_boolean() ? "true" : "false";
            break;
        default:
            // unreachable given is_specifier_end
            throw std::runtime_error("format: unknown specifier");
        }
    }
    return out;
}
