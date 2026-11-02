#include "jbd_bms.h"
#include "../utils/string_utils.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "module_helpers.h"
#include <algorithm>
#include <memory>
#include <stdexcept>

static constexpr uint8_t FRAME_START = 0xDD;
static constexpr uint8_t FRAME_END = 0x77;
static constexpr uint8_t READ = 0xA5;
static constexpr uint8_t BASIC_INFO = 0x03;
static constexpr uint8_t CELL_VOLTAGES = 0x04;
static constexpr uint8_t HARDWARE_VERSION = 0x05;
static constexpr size_t BASIC_INFO_LENGTH = 23;        // without the temperatures
static constexpr unsigned long REPLY_TIMEOUT_MS = 500; // an answer of 40 bytes takes 42 ms at 9600 baud

static Module_ptr create_jbd_bms(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler) {
    Module::expect(arguments, 1, identifier);
    const ConstSerial_ptr serial = get_module_argument<const Serial>(arguments[0]);
    return std::make_shared<JbdBms>(name, serial);
}
REGISTER_MODULE(JbdBms, &create_jbd_bms)

const std::map<std::string, Variable_ptr> JbdBms::get_defaults() {
    return {
        {"voltage", std::make_shared<NumberVariable>()},
        {"current", std::make_shared<NumberVariable>()},
        {"residual_capacity", std::make_shared<NumberVariable>()},
        {"nominal_capacity", std::make_shared<NumberVariable>()},
        {"cycles", std::make_shared<IntegerVariable>()},
        {"percentage", std::make_shared<IntegerVariable>()},
        {"protections", std::make_shared<IntegerVariable>()},
        {"fets", std::make_shared<IntegerVariable>()},
        {"cells", std::make_shared<IntegerVariable>()},
        {"temperature", std::make_shared<NumberVariable>()},
        {"last_message_age", std::make_shared<IntegerVariable>()},
        {"errors", std::make_shared<IntegerVariable>()},
        {"interval", std::make_shared<NumberVariable>(1.0)},
    };
}

JbdBms::JbdBms(const std::string &name, const ConstSerial_ptr serial) : Module(name), serial(serial) {
    this->properties = JbdBms::get_defaults();
    this->voltage = this->properties.at("voltage");
    this->current = this->properties.at("current");
    this->residual_capacity = this->properties.at("residual_capacity");
    this->nominal_capacity = this->properties.at("nominal_capacity");
    this->cycles = this->properties.at("cycles");
    this->percentage = this->properties.at("percentage");
    this->protections = this->properties.at("protections");
    this->fets = this->properties.at("fets");
    this->cells = this->properties.at("cells");
    this->temperature = this->properties.at("temperature");
    this->last_message_age = this->properties.at("last_message_age");
    this->errors = this->properties.at("errors");
    this->interval = this->properties.at("interval");
    this->reply_millis = millis();
    this->serial->claim(name);
}

void JbdBms::step() {
    this->receive();
    if (this->requested_register && millis_since(this->request_millis) > REPLY_TIMEOUT_MS) {
        this->count_error();
    }
    const double interval = this->interval->number_value();
    if (!this->requested_register && interval > 0 && millis_since(this->request_millis) >= interval * 1000) {
        this->request(BASIC_INFO);
    }
    this->last_message_age->set_integer_value(millis_since(this->reply_millis));
    Module::step();
}

void JbdBms::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "cell_voltages") {
        Module::expect(arguments, 0);
        this->request(CELL_VOLTAGES);
    } else if (method_name == "hardware_version") {
        Module::expect(arguments, 0);
        this->request(HARDWARE_VERSION);
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
    const uint16_t checksum = (data[length] << 8) | data[length + 1];
    if (data[length + 2] != FRAME_END || static_cast<uint16_t>(0x10000 - sum) != checksum || status != 0 ||
        reg != this->requested_register) {
        this->count_error();
        return;
    }
    if (reg == BASIC_INFO) {
        if (length < BASIC_INFO_LENGTH) {
            this->count_error();
            return;
        }
        this->handle_basic_info(data, length);
    } else if (reg == CELL_VOLTAGES) {
        char line[512];
        int pos = csprintf(line, sizeof(line), "%s cell voltages:", this->name.c_str());
        for (size_t i = 0; i + 1 < length && pos < static_cast<int>(sizeof(line)) - 8; i += 2) {
            pos += csprintf(&line[pos], sizeof(line) - pos, " %.3f", ((data[i] << 8) | data[i + 1]) / 1000.0);
        }
        echo("%s", line);
    } else if (reg == HARDWARE_VERSION) {
        echo("%s hardware version: %.*s", this->name.c_str(), static_cast<int>(length), reinterpret_cast<const char *>(data));
    }
    this->requested_register = 0;
    this->reply_millis = millis();
}

void JbdBms::handle_basic_info(const uint8_t *data, const size_t length) {
    const auto word = [data](const size_t i) { return static_cast<uint16_t>((data[i] << 8) | data[i + 1]); };
    this->voltage->set_number_value(word(0) / 100.0);
    this->current->set_number_value(static_cast<int16_t>(word(2)) / 100.0);
    this->residual_capacity->set_number_value(word(4) / 100.0);
    this->nominal_capacity->set_number_value(word(6) / 100.0);
    this->cycles->set_integer_value(word(8));
    this->protections->set_integer_value(word(16));
    this->percentage->set_integer_value(data[19]);
    this->fets->set_integer_value(data[20]);
    this->cells->set_integer_value(data[21]);
    const size_t sensors = std::min(static_cast<size_t>(data[22]), (length - BASIC_INFO_LENGTH) / 2);
    if (sensors > 0) {
        double sum = 0;
        for (size_t i = 0; i < sensors; ++i) {
            sum += word(BASIC_INFO_LENGTH + 2 * i) / 10.0 - 273.15;
        }
        this->temperature->set_number_value(sum / sensors);
    }
}

void JbdBms::count_error() {
    this->errors->set_integer_value(this->errors->integer_value() + 1);
    this->requested_register = 0;
    this->frame_length = 0;
}
