#include "error_handling.h"

std::map<std::string, Error_code> Error_handling::error_codes_;
bool Error_handling::has_error_;

Error_code Error_handling::get_error(const std::string module_name) {
    return error_codes_[module_name];
}

std::map<std::string, Error_code> Error_handling::get_errors() {
    return error_codes_;
}

bool Error_handling::has_error() {
    return has_error_;
}

void Error_handling::set_error(const std::string module_name, const Error_code error_code) {
    error_codes_[module_name] = error_code;
    has_error_ = true;
}
