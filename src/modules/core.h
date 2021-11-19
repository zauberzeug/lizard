#pragma once

#include <list>
#include <utility>
#include "module.h"

struct output_element_t
{
    Module *module;
    std::string property_name;
    unsigned int precision;
};

class Core : public Module
{
private:
    std::list<struct output_element_t> output_list;

public:
    bool debug = false;

    Core(std::string name);
    void call(std::string method, std::vector<Argument *> arguments);
    double get(std::string property_name);
    void set(std::string property_name, double value);
    std::string get_output();
};