#include "global_error_state.h"

bool GlobalErrorState::has_error_ = false;
std::vector<ModuleErrorInfo> GlobalErrorState::modules_;

bool GlobalErrorState::has_error() {
    return has_error_;
}

void GlobalErrorState::set_error_flag(bool has_error) {
    has_error_ = has_error;
}

void GlobalErrorState::register_module(const char *module_name, GetModuleErrorFunc get_error) {
    modules_.push_back({module_name, get_error});
}

std::vector<std::string> GlobalErrorState::get_all_errors() {
    std::vector<std::string> all_errors;

    for (const auto &module_info : modules_) {
        std::string module_error = module_info.get_error();
        if (!module_error.empty()) {
            all_errors.push_back(module_error);
        }
    }

    return all_errors;
}

void GlobalErrorState::clear_all_errors() {
    // Note: This would need each module to provide a clear function
    // For now, just reset the global flag
    has_error_ = false;
}
