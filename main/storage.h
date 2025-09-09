#pragma once

#include <cstdint>
#include <string>

class Storage {
private:
    static void put(const std::string value);
    static std::string get();

public:
    static std::string startup;

    static void init();
    static void append_to_startup(const std::string line);
    static void remove_from_startup(const std::string substring = "");
    static void print_startup(const std::string substring = "");
    static void save_startup();
    static void clear_nvs();

    static void set_user_pin(std::uint32_t pin);
    static bool get_user_pin(std::uint32_t &pin);
    static void remove_user_pin();
};
