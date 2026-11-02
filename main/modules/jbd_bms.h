#pragma once

#include "module.h"
#include "serial.h"
#include <cstdint>

// A battery management system with the JBD protocol (frames from 0xDD to 0x77) on a serial: the module asks for its basic
// information every `interval` seconds and keeps the values of the answer in its properties.
class JbdBms : public Module {
private:
    static constexpr size_t MAX_FRAME_LENGTH = 4 + 255 + 3; // start, register, status and length, the data, checksum and end

    const ConstSerial_ptr serial;
    uint8_t frame[MAX_FRAME_LENGTH];
    size_t frame_length = 0;
    uint8_t requested_register = 0; // 0 while no answer is awaited
    unsigned long request_millis = 0;
    unsigned long reply_millis = 0;

    Variable_ptr voltage;
    Variable_ptr current;
    Variable_ptr residual_capacity;
    Variable_ptr nominal_capacity;
    Variable_ptr cycles;
    Variable_ptr percentage;
    Variable_ptr protections;
    Variable_ptr fets;
    Variable_ptr cells;
    Variable_ptr temperature;
    Variable_ptr last_message_age;
    Variable_ptr errors;
    Variable_ptr interval;

    void request(const uint8_t reg);
    void receive();
    void handle_frame();
    void handle_basic_info(const uint8_t *data, const size_t length);
    void count_error(); // also gives up the awaited answer

public:
    static inline constexpr const char *TYPE = "JbdBms";

    JbdBms(const std::string &name, const ConstSerial_ptr serial);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
