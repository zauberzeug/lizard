#include "error_handling.h"

std::map<std::string, ErrorCode> ErrorHandling::error_codes_;
bool ErrorHandling::has_error_;

ErrorCode ErrorHandling::get_error(const std::string module_name) {
    return error_codes_[module_name];
}

std::map<std::string, ErrorCode> ErrorHandling::get_errors() {
    return error_codes_;
}

bool ErrorHandling::has_error() {
    return has_error_;
}

void ErrorHandling::set_error(const std::string module_name, const ErrorCode error_code) {
    error_codes_[module_name] = error_code;
    has_error_ = true;
}
