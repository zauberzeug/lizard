#pragma once

#include "module.h"
#include <memory>
#include <utility>

struct output_element_t {
    const ConstModule_ptr module;
    const std::string property_name;
    const unsigned int precision;
};

class Core;
using Core_ptr = std::shared_ptr<Core>;

class Core : public Module {
private:
    std::list<struct output_element_t> output_list;
    unsigned long int last_message_millis = 0;
    uint8_t expander_id = 255;
    bool is_external_expander = false;

public:
    Core(const std::string name);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    double get(const std::string property_name) const;
    void set(std::string property_name, double value);
    std::string get_output() const override;
    void keep_alive();
    uint8_t get_expander_id() const;
    void set_expander_id(uint8_t id);
    void set_external_mode(bool external);
    bool is_external() const;
};
