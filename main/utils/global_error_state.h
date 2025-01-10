#pragma once

class GlobalErrorState {
public:
    static bool has_error();
    static void set_error();

private:
    static bool has_error_;
};