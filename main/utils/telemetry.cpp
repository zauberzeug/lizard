#include "telemetry.h"
#include "../compilation/expressions.h"
#include "../global.h"
#include "interpreter_lock.h"
#include "timing.h"
#include "uart.h"
#include "esp_rom_crc.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace telemetry {

constexpr size_t HEADER_SIZE = 6; // frame ID, sequence number, millis
constexpr size_t MAX_PAYLOAD = 180;
constexpr size_t CRC_SIZE = 2;
constexpr size_t MAX_BODY = HEADER_SIZE + MAX_PAYLOAD + CRC_SIZE;
constexpr int64_t MAX_INTERVAL = 3600000;
constexpr size_t MAX_FRAMES = 255;
constexpr size_t LAYOUT_LINES_PER_STEP = 4; // spread, so announcing a large frame does not stall one step

struct Field {
    ConstVariable_ptr variable;
    std::string name;
};

struct Frame {
    std::vector<Field> fields;
    size_t bits_offset = 0; // the bools follow the floats and ints as bits
    size_t payload_size = 0;
    unsigned long interval = 0;
    unsigned long last_millis = 0;
    uint8_t sequence = 0;
    size_t layout_lines_sent = 0;
    bool announced = false; // all layout lines were sent once, so readers can decode the frame
};

static std::vector<Frame> frames; // frame ID = index + 1

static size_t encode_base64(const uint8_t *data, const size_t length, char *text) {
    static constexpr char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t pos = 0;
    for (size_t i = 0; i < length; i += 3) {
        const uint32_t chunk = data[i] << 16 | (i + 1 < length ? data[i + 1] << 8 : 0) | (i + 2 < length ? data[i + 2] : 0);
        text[pos++] = ALPHABET[chunk >> 18 & 0x3f];
        text[pos++] = ALPHABET[chunk >> 12 & 0x3f];
        text[pos++] = i + 1 < length ? ALPHABET[chunk >> 6 & 0x3f] : '=';
        text[pos++] = i + 2 < length ? ALPHABET[chunk & 0x3f] : '=';
    }
    return pos;
}

static char type_letter(const Type type) {
    switch (type) {
    case boolean:
        return '?';
    case integer:
        return 'i';
    default:
        return 'f';
    }
}

static Field to_field(const ConstExpression_ptr &argument) {
    Field field;
    if (const auto property = std::dynamic_pointer_cast<const PropertyExpression>(argument)) {
        field = {property->module->get_property(property->property_name), property->module->name + "." + property->property_name};
    } else if (const auto variable = std::dynamic_pointer_cast<const VariableExpression>(argument)) {
        for (auto const &[name, candidate] : Global::variables) {
            if (candidate == variable->variable) {
                field = {variable->variable, name};
                break;
            }
        }
    }
    if (!field.variable) {
        throw std::runtime_error("telemetry fields must be properties or variables");
    }
    if (!argument->is_numbery()) {
        throw std::runtime_error("telemetry field \"" + field.name + "\" is not a bool, int or float");
    }
    return field;
}

void add_frame(const std::vector<ConstExpression_ptr> &arguments) {
    if (frames.size() >= MAX_FRAMES) {
        throw std::runtime_error("too many telemetry frames");
    }
    Frame frame;
    size_t field_count = arguments.size();
    if (field_count > 1 && std::dynamic_pointer_cast<const IntegerExpression>(arguments.back())) {
        const int64_t interval = arguments.back()->evaluate_integer();
        if (interval < 0 || interval > MAX_INTERVAL) {
            throw std::runtime_error("telemetry interval must be between 0 and 3600000 ms");
        }
        frame.interval = interval;
        --field_count;
    }
    if (field_count == 0) {
        throw std::runtime_error("telemetry frames need at least one field");
    }
    frame.fields.reserve(field_count);
    size_t bools = 0;
    for (size_t i = 0; i < field_count; ++i) {
        frame.fields.push_back(to_field(arguments[i]));
        bools += frame.fields.back().variable->type == boolean;
    }
    frame.bits_offset = 4 * (field_count - bools);
    frame.payload_size = frame.bits_offset + (bools + 7) / 8;
    if (frame.payload_size > MAX_PAYLOAD) {
        throw std::runtime_error("too many fields for one frame");
    }
    frames.push_back(std::move(frame));
}

void announce() {
    for (Frame &frame : frames) {
        frame.layout_lines_sent = 0;
    }
}

void clear() {
    frames.clear();
}

static void send_layout_lines(const unsigned long now) {
    size_t budget = LAYOUT_LINES_PER_STEP;
    for (size_t i = 0; i < frames.size() && budget > 0; ++i) {
        Frame &frame = frames[i];
        for (; frame.layout_lines_sent < frame.fields.size() && budget > 0; --budget) {
            const size_t index = frame.layout_lines_sent++;
            const Field &field = frame.fields[index];
            echo("__LAYOUT__v1 %d.%d/%d %s:%c", static_cast<int>(i + 1), static_cast<int>(index),
                 static_cast<int>(frame.fields.size()), field.name.c_str(), type_letter(field.variable->type));
        }
        if (!frame.announced && frame.layout_lines_sent == frame.fields.size()) {
            frame.announced = true;
            frame.last_millis = now - frame.interval; // due right away, however long the layout took
        }
    }
}

// body = frame ID, sequence number, millis, payload, CRC; multi-byte values are little-endian like the ESP32
static void send_frame(const uint8_t id, Frame &frame, const uint32_t now) {
    uint8_t body[MAX_BODY];
    body[0] = id;
    body[1] = frame.sequence++;
    memcpy(&body[2], &now, 4);
    uint8_t *const payload = &body[HEADER_SIZE];
    memset(&payload[frame.bits_offset], 0, frame.payload_size - frame.bits_offset);
    size_t offset = 0;
    size_t bit = 0;
    for (const Field &field : frame.fields) {
        switch (field.variable->type) {
        case number: {
            const float value = field.variable->number_value();
            memcpy(&payload[offset], &value, 4);
            offset += 4;
            break;
        }
        case integer: {
            const int32_t value = std::clamp<int64_t>(field.variable->integer_value(), INT32_MIN, INT32_MAX);
            memcpy(&payload[offset], &value, 4);
            offset += 4;
            break;
        }
        default:
            if (field.variable->boolean_value()) {
                payload[frame.bits_offset + bit / 8] |= 1 << bit % 8;
            }
            ++bit;
        }
    }
    const size_t length = HEADER_SIZE + frame.payload_size;
    const uint16_t crc = ~esp_rom_crc16_be(static_cast<uint16_t>(~0xffff), body, length); // CRC-16/CCITT-FALSE
    memcpy(&body[length], &crc, CRC_SIZE);

    char text[(MAX_BODY + 2) / 3 * 4 + 1];
    text[encode_base64(body, length + CRC_SIZE, text)] = '\0';
    echo("~%s", text);
}

void step() {
    InterpreterLock lock;
    const unsigned long now = millis();
    send_layout_lines(now);
    for (size_t i = 0; i < frames.size(); ++i) {
        Frame &frame = frames[i];
        if (frame.announced && now - frame.last_millis >= frame.interval) {
            // keep the rate when steps run late, catching up at most one interval
            frame.last_millis = now - frame.last_millis < 2 * frame.interval ? frame.last_millis + frame.interval : now - frame.interval;
            send_frame(i + 1, frame, now);
        }
    }
}

} // namespace telemetry
