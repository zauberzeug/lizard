#pragma once

#include "module.h"
#include <memory>
#include <utility>

struct output_element_t {
    const ConstModule_ptr module;
    const std::string property_name;
    const unsigned int precision;
};

struct frame_field_t {
    const ConstModule_ptr module;
    const std::string property_name;
    const char type; // ? b B h H i I f
    const double scale;
};

struct frame_t {
    uint8_t id;
    unsigned long interval;
    unsigned long last_millis;
    uint8_t seq;
    std::vector<frame_field_t> fields;
};

class Core;
using Core_ptr = std::shared_ptr<Core>;

class Core : public Module {
private:
    std::list<struct output_element_t> output_list;
    std::vector<frame_t> frames;
    void emit_frame(frame_t &frame, unsigned long now);
    void parse_frame_fields(std::string format, std::vector<frame_field_t> &fields) const;
    unsigned long int last_message_millis = 0;

public:
    Core(const std::string name);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    double get(const std::string property_name) const;
    void set(std::string property_name, double value);
    std::string get_output() const override;
    void keep_alive();
};
