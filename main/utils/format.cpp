#include "format.h"

#include "../compilation/type.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>

std::string format_args(const std::string &fmt,
                        const std::vector<ConstExpression_ptr> &arguments,
                        size_t args_start) {
    std::string out;
    size_t arg_idx = args_start;
    char buf[256];
    for (size_t i = 0; i < fmt.size();) {
        if (fmt[i] != '%') {
            out += fmt[i++];
            continue;
        }
        const size_t start = i++;
        while (i < fmt.size() && !std::strchr("dfs%", fmt[i])) {
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
            throw std::runtime_error("format: '" + sub + "' has no matching argument");
        }
        const auto &arg = arguments[arg_idx++];
        if (spec == 'd') {
            std::snprintf(buf, sizeof(buf), sub.c_str(), static_cast<int>(arg->evaluate_integer()));
        } else if (spec == 'f') {
            std::snprintf(buf, sizeof(buf), sub.c_str(), arg->evaluate_number());
        } else if (arg->type & boolean) {
            std::snprintf(buf, sizeof(buf), sub.c_str(), arg->evaluate_boolean() ? "true" : "false");
        } else {
            std::snprintf(buf, sizeof(buf), sub.c_str(), arg->evaluate_string().c_str());
        }
        out += buf;
    }
    return out;
}
