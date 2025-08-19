#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

using ErrorCode = uint8_t;

// Function type for getting error string from a module
using GetModuleErrorFunc = std::function<std::string()>;

struct ModuleErrorInfo {
    const char *module_name;
    GetModuleErrorFunc get_error;
};

class GlobalErrorState {
public:
    static bool has_error();
    static void set_error_flag(bool has_error);
    static void register_module(const char *module_name, GetModuleErrorFunc get_error);
    static std::vector<std::string> get_all_errors();
    static void clear_all_errors(); // For testing purposes only

private:
    static bool has_error_;
    static std::vector<ModuleErrorInfo> modules_;
};
