#include "jbd_bms.h"
#include "../utils/string_utils.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "module_helpers.h"
#include <algorithm>
#include <memory>

static constexpr uint8_t FRAME_START = 0xDD;
static constexpr uint8_t FRAME_END = 0x77;
static constexpr uint8_t READ = 0xA5;
static constexpr uint8_t BASIC_INFO = 0x03;
static constexpr uint8_t CELL_VOLTAGES = 0x04;
static constexpr uint8_t HARDWARE_VERSION = 0x05;
static constexpr size_t BASIC_INFO_LENGTH = 23;         // without the temperatures
static constexpr unsigned long ANSWER_TIMEOUT_MS = 500; // an answer of 40 bytes takes 42 ms at 9600 baud

static Module_ptr create_jbd_bms(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler) {
    Module::expect(arguments, 1, identifier);
    const ConstSerial_ptr serial = get_module_argument<const Serial>(arguments[0]);
    return std::make_shared<JbdBms>(name, serial);
}
REGISTER_MODULE(JbdBms, &create_jbd_bms)

// numbers in JBD frames are big-endian
static uint16_t word(const uint8_t *data, const size_t i) {
    return data[i] << 8 | data[i + 1];
}

const std::map<std::string, Variable_ptr> JbdBms::get_defaults() {
    return {
        {"voltage", std::make_shared<NumberVariable>()},
        {"current", std::make_shared<NumberVariable>()},
        {"residual_capacity", std::make_shared<NumberVariable>()},
        {"nominal_capacity", std::make_shared<NumberVariable>()},
        {"percentage", std::make_shared<IntegerVariable>()},
        {"cycles", std::make_shared<IntegerVariable>()},
        {"temperature", std::make_shared<NumberVariable>()},
        {"protections", std::make_shared<IntegerVariable>()},
        {"fets", std::make_shared<IntegerVariable>()},
        {"cells", std::make_shared<IntegerVariable>()},
        {"interval", std::make_shared<NumberVariable>(1.0)},
        {"last_message_age", std::make_shared<IntegerVariable>()},
        {"errors", std::make_shared<IntegerVariable>()},
    };
}

JbdBms::JbdBms(const std::string &name, const ConstSerial_ptr serial) : Module(name), serial(serial) {
    this->properties = JbdBms::get_defaults();
    this->serial->claim(name);
}

void JbdBms::step() {
    this->receive();
    if (this->requested_register && millis_since(this->request_millis) > ANSWER_TIMEOUT_MS) {
        this->count_error();
    }
    if (!this->requested_register) {
        const double interval = this->properties.at("interval")->number_value();
        if (!this->queued_registers.empty()) {
            const auto next = this->queued_registers.begin();
            this->request(*next);
            this->queued_registers.erase(next);
        } else if (interval > 0 && millis_since(this->request_millis) >= interval * 1000) {
            this->request(BASIC_INFO);
        }
    }
    this->properties.at("last_message_age")->set_integer_value(millis_since(this->last_message_millis));
    Module::step();
}

void JbdBms::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "cell_voltages") {
        Module::expect(arguments, 0);
        this->queued_registers.insert(CELL_VOLTAGES);
    } else if (method_name == "hardware_version") {
        Module::expect(arguments, 0);
        this->queued_registers.insert(HARDWARE_VERSION);
    } else {
        Module::call(method_name, arguments);
    }
}

void JbdBms::request(const uint8_t reg) {
    const uint16_t checksum = 0x10000 - reg; // of the register and the data length 0
    const uint8_t bytes[] = {FRAME_START, READ, reg, 0x00, static_cast<uint8_t>(checksum >> 8), static_cast<uint8_t>(checksum), FRAME_END};
    this->serial->flush(); // the rest of an answer that came too late would precede the next one
    for (const uint8_t byte : bytes) {
        this->serial->write(byte);
    }
    this->frame_length = 0;
    this->requested_register = reg;
    this->request_millis = millis();
}

void JbdBms::receive() {
    // the BMS may pause within an answer, so it is collected across steps
    int byte;
    while ((byte = this->serial->read()) >= 0) {
        if (!this->requested_register) {
            continue; // e.g. a late answer, which counted at its timeout
        }
        if (this->frame_length == 0 && byte != FRAME_START) {
            continue;
        }
        this->frame[this->frame_length++] = byte;
        if (this->frame_length >= 4 && this->frame_length == 4 + static_cast<size_t>(this->frame[3]) + 3) {
            this->handle_frame();
            this->frame_length = 0;
        }
    }
}

void JbdBms::handle_frame() {
    const uint8_t reg = this->frame[1];
    const uint8_t status = this->frame[2];
    const size_t length = this->frame[3];
    const uint8_t *const data = &this->frame[4];
    uint16_t sum = status + length;
    for (size_t i = 0; i < length; ++i) {
        sum += data[i];
    }
    if (data[length + 2] != FRAME_END || static_cast<uint16_t>(0x10000 - sum) != word(data, length) || status != 0 ||
        reg != this->requested_register || (reg == BASIC_INFO && length < BASIC_INFO_LENGTH)) {
        this->count_error();
        return;
    }
    this->requested_register = 0;
    if (reg == BASIC_INFO) {
        this->handle_basic_info(data, length);
    } else if (reg == CELL_VOLTAGES) {
        char line[512];
        int pos = csprintf(line, sizeof(line), "%s cell voltages:", this->name.c_str());
        for (size_t i = 0; i + 1 < length && pos < static_cast<int>(sizeof(line)) - 8; i += 2) {
            pos += csprintf(&line[pos], sizeof(line) - pos, " %.3f", word(data, i) / 1000.0);
        }
        echo("%s", line);
    } else if (reg == HARDWARE_VERSION) {
        echo("%s hardware version: %.*s", this->name.c_str(), static_cast<int>(length), reinterpret_cast<const char *>(data));
    }
}

void JbdBms::handle_basic_info(const uint8_t *data, const size_t length) {
    this->properties.at("voltage")->set_number_value(word(data, 0) / 100.0);
    this->properties.at("current")->set_number_value(static_cast<int16_t>(word(data, 2)) / 100.0);
    this->properties.at("residual_capacity")->set_number_value(word(data, 4) / 100.0);
    this->properties.at("nominal_capacity")->set_number_value(word(data, 6) / 100.0);
    this->properties.at("cycles")->set_integer_value(word(data, 8));
    this->properties.at("protections")->set_integer_value(word(data, 16));
    this->properties.at("percentage")->set_integer_value(data[19]);
    this->properties.at("fets")->set_integer_value(data[20]);
    this->properties.at("cells")->set_integer_value(data[21]);
    const size_t sensors = std::min(static_cast<size_t>(data[22]), (length - BASIC_INFO_LENGTH) / 2);
    if (sensors > 0) {
        double sum = 0;
        for (size_t i = 0; i < sensors; ++i) {
            sum += word(data, BASIC_INFO_LENGTH + 2 * i) / 10.0 - 273.15;
        }
        this->properties.at("temperature")->set_number_value(sum / sensors);
    }
    this->last_message_millis = millis();
}

void JbdBms::count_error() {
    this->properties.at("errors")->set_integer_value(this->properties.at("errors")->integer_value() + 1);
    this->requested_register = 0;
    this->frame_length = 0;
}
