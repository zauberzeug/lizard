#include "global_error_state.h"

bool GlobalErrorState::has_error_ = false;

bool GlobalErrorState::has_error() {
    return has_error_;
}

void GlobalErrorState::set_error() {
    has_error_ = true;
}