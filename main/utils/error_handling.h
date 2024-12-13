#pragma once

#include <map>
#include <string>

enum Error_code {
    ERROR_NONE = 0,
    ERROR_CONNECTION_TIMEOUT = 1,
    ERROR_CONNECTION_FAILED = 2
};

class Error_handling {
public:
    static Error_code get_error(const std::string module_name);
    static std::map<std::string, Error_code> get_errors();
    static bool has_error();
    static void set_error(const std::string module_name, const Error_code error_code);

private:
    // module name, error code
    static std::map<std::string, Error_code> error_codes_;
    static bool has_error_;
};
