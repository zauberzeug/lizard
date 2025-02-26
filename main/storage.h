#pragma once

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

    // Device ID functions
    static void put_device_id(const char id[2]);
    static void get_device_id(char id[2]);
};
