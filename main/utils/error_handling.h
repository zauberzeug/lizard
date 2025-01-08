#pragma once

#include <map>
#include <string>

enum ErrorCode {
    ERROR_NONE = 0,
    ERROR_CONNECTION_TIMEOUT = 1,
    ERROR_CONNECTION_FAILED = 2,
};

class ErrorHandling {
public:
    static ErrorCode get_error(const std::string module_name);
    static std::map<std::string, ErrorCode> get_errors();
    static bool has_error();
    static void set_error(const std::string module_name, const ErrorCode error_code);

private:
    // module name, error code
    static std::map<std::string, ErrorCode> error_codes_;
    static bool has_error_;
};
