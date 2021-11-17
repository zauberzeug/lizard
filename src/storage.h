#pragma once

#include <string>

class Storage
{
private:
    static void put(std::string value);
    static std::string get();

public:
    static std::string startup;

    static void init();
    static void append_to_startup(std::string line);
    static void remove_from_startup(std::string substring = "");
    static void print_startup(std::string substring = "");
    static void save_startup();
};