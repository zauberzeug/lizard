#pragma once

#include "module.h"
#include "serial.h"
#include <cstdint>
#include <set>

class JbdBms;
using JbdBms_ptr = std::shared_ptr<JbdBms>;

class JbdBms : public Module {
private:
    static constexpr size_t MAX_FRAME_LENGTH = 4 + 255 + 3; // start, register, status and length, the data, checksum and end

    const ConstSerial_ptr serial;
    uint8_t frame[MAX_FRAME_LENGTH];
    size_t frame_length = 0;
    uint8_t requested_register = 0;     // 0 while no answer is awaited
    std::set<uint8_t> queued_registers; // asked for by methods, requested once no answer is awaited
    unsigned long request_millis = 0;
    unsigned long last_message_millis = 0;

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
