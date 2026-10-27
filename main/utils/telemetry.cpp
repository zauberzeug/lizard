#include "telemetry.h"
#include "../compilation/expressions.h"
#include "../global.h"
#include "../modules/serial_bus.h"
#include "interpreter_lock.h"
#include "timing.h"
#include "uart.h"
#include "esp_rom_crc.h"
#include "mbedtls/base64.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace telemetry {

constexpr int64_t MAX_INTERVAL = 3600000;
constexpr size_t MAX_FRAMES = 255;
constexpr size_t MAX_FIELDS = 8 * MAX_PAYLOAD; // all of them bools
constexpr size_t MAX_NAME = 200;               // keeps every layout line within one serial bus payload
constexpr size_t LAYOUT_LINES_PER_STEP = 4;    // spread, so announcing a large frame does not stall one step
static_assert(sizeof("__LAYOUT__v1 255.1439/1440 :f") + MAX_NAME <= SerialBus::PAYLOAD_CAPACITY);
constexpr size_t MAX_LINE = 1 + (MAX_BODY + 2) / 3 * 4; // "~" and the Base64 text, without terminator
static_assert(MAX_LINE < SerialBus::PAYLOAD_CAPACITY);

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
static std::vector<SerialBus *> buses;
static uint8_t coordinator = 0; // the node the layout lines went to, 0 for the console

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

static uint16_t crc16(const uint8_t *data, const size_t length) {
    return ~esp_rom_crc16_be(static_cast<uint16_t>(~0xffff), data, length); // CRC-16/CCITT-FALSE
}

// 4 bytes per float or int, then the bools as bits
static size_t payload_size(const size_t fields, const size_t bools) {
    return 4 * (fields - bools) + (bools + 7) / 8;
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
    if (field.name.size() > MAX_NAME) {
        throw std::runtime_error("telemetry field \"" + field.name + "\" has a name longer than " + std::to_string(MAX_NAME) + " characters");
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
    frame.payload_size = payload_size(field_count, bools);
    if (frame.payload_size > MAX_PAYLOAD) {
        throw std::runtime_error("too many fields for one frame");
    }
    frames.push_back(std::move(frame));
}

void announce() {
    for (Frame &frame : frames) {
        frame.layout_lines_sent = 0;
    }
    // a host that reads peers through this node gets their layout lines the same way
    for (SerialBus *const bus : buses) {
        bus->request_passed_layouts();
    }
}

void clear() {
    frames.clear();
    for (SerialBus *const bus : buses) {
        bus->clear_telemetry_slots();
    }
}

void add_bus(SerialBus *const bus) {
    buses.push_back(bus);
}

void remove_bus(SerialBus *const bus) {
    buses.erase(std::remove(buses.begin(), buses.end(), bus), buses.end());
}

static SerialBus *polled_bus() {
    for (SerialBus *const bus : buses) {
        if (bus->coordinator() != 0) {
            return bus;
        }
    }
    return nullptr;
}

// false while the bus queue is full
static bool send_layout_line(SerialBus *const bus, const uint8_t id, const Frame &frame, const size_t index) {
    const Field &field = frame.fields[index];
    char line[SerialBus::PAYLOAD_CAPACITY];
    const int length = std::snprintf(line, sizeof(line), "%sv%d %d.%d/%d %s:%c", LAYOUT_PREFIX, LAYOUT_VERSION, id,
                                     static_cast<int>(index), static_cast<int>(frame.fields.size()), field.name.c_str(),
                                     type_letter(field.variable->type));
    if (!bus) {
        echo("%s", line);
        return true;
    }
    return bus->send_telemetry_layout(line, length);
}

// true once all layout lines are out
static bool send_layout_lines(SerialBus *const bus, const unsigned long now) {
    size_t budget = LAYOUT_LINES_PER_STEP;
    for (size_t i = 0; i < frames.size(); ++i) {
        Frame &frame = frames[i];
        for (; frame.layout_lines_sent < frame.fields.size() && budget > 0; --budget) {
            if (!send_layout_line(bus, i + 1, frame, frame.layout_lines_sent)) {
                return false; // the queue empties with the next poll
            }
            ++frame.layout_lines_sent;
        }
        if (frame.layout_lines_sent < frame.fields.size()) {
            return false;
        }
        if (!frame.announced) {
            frame.announced = true;
            frame.last_millis = now - frame.interval; // due right away, however long the layout took
        }
    }
    return true;
}

// body = frame ID, sequence number, millis, payload, CRC; multi-byte values are little-endian like the ESP32
static size_t encode_frame(const uint8_t id, const Frame &frame, const uint8_t sequence, const uint32_t now, char *text) {
    uint8_t body[MAX_BODY];
    body[0] = id;
    body[1] = sequence;
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
    const uint16_t crc = crc16(body, length);
    memcpy(&body[length], &crc, CRC_SIZE);

    text[0] = FRAME_PREFIX;
    const size_t text_length = 1 + encode_base64(body, length + CRC_SIZE, &text[1]);
    text[text_length] = '\0';
    return text_length;
}

static void send_frame(SerialBus *const bus, const uint8_t id, Frame &frame, const uint32_t now) {
    char text[MAX_LINE + 1];
    if (!bus) {
        encode_frame(id, frame, frame.sequence++, now, text);
        echo("%s", text);
        return;
    }
    // the newest frame waits in its slot for the next poll; a replaced frame keeps its sequence number, so a gap is a loss
    const bool replace = bus->telemetry_frame_pending(id);
    const uint8_t sequence = replace ? static_cast<uint8_t>(frame.sequence - 1) : frame.sequence;
    const size_t length = encode_frame(id, frame, sequence, now, text);
    if (bus->store_telemetry_frame(id, text, length, replace) && !replace) {
        ++frame.sequence;
    }
}

void step() {
    InterpreterLock lock;
    SerialBus *const bus = polled_bus();
    const uint8_t polling = bus ? bus->coordinator() : 0;
    if (polling != coordinator) {
        // a coordinator started polling this node: it has not seen the layout lines, which went to the console
        coordinator = polling;
        for (Frame &frame : frames) {
            frame.layout_lines_sent = 0;
            frame.announced = false;
        }
    }
    const unsigned long now = millis();
    if (!send_layout_lines(bus, now) && bus) {
        return; // frames would crowd the layout lines out of the coordinator's inbound queue
    }
    for (size_t i = 0; i < frames.size(); ++i) {
        Frame &frame = frames[i];
        if (frame.announced && now - frame.last_millis >= frame.interval) {
            // keep the rate when steps run late, catching up at most one interval
            frame.last_millis = now - frame.last_millis < 2 * frame.interval ? frame.last_millis + frame.interval : now - frame.interval;
            send_frame(bus, i + 1, frame, now);
        }
    }
}

size_t decode_frame(const char *line, const size_t length, uint8_t *body) {
    size_t body_length = 0;
    if (length < 1 || line[0] != FRAME_PREFIX ||
        mbedtls_base64_decode(body, MAX_BODY, &body_length, reinterpret_cast<const unsigned char *>(&line[1]), length - 1) != 0 ||
        body_length < HEADER_SIZE + CRC_SIZE) {
        return 0;
    }
    uint16_t crc;
    memcpy(&crc, &body[body_length - CRC_SIZE], CRC_SIZE);
    return crc == crc16(body, body_length - CRC_SIZE) ? body_length : 0;
}

// reads 1 to 5 decimal digits
static bool read_number(const char *&c, size_t &value) {
    const char *const start = c;
    value = 0;
    while (std::isdigit(static_cast<unsigned char>(*c)) && c - start < 5) {
        value = 10 * value + (*c++ - '0');
    }
    return c > start && !std::isdigit(static_cast<unsigned char>(*c));
}

// "__LAYOUT__v<version> <frame>.<index>/<count> <name>:<type>"
bool parse_layout(const char *line, LayoutLine &layout) {
    const char *c = line + sizeof(LAYOUT_PREFIX) - 1;
    size_t version, frame_id, index, count;
    if (*c++ != 'v' || !read_number(c, version)) {
        return false;
    }
    layout.version = version;
    if (version != LAYOUT_VERSION) {
        return true;
    }
    if (*c++ != ' ' || !read_number(c, frame_id) || *c++ != '.' || !read_number(c, index) || *c++ != '/' ||
        !read_number(c, count) || *c++ != ' ') {
        return false;
    }
    const char *const colon = std::strrchr(c, ':');
    if (frame_id == 0 || frame_id > MAX_FRAMES || count > MAX_FIELDS || index >= count || !colon || colon == c ||
        colon[1] == '\0' || colon[2] != '\0') {
        return false;
    }
    layout.type = colon[1];
    if (layout.type != 'f' && layout.type != 'i' && layout.type != '?') {
        return false;
    }
    layout.frame_id = frame_id;
    layout.index = index;
    layout.count = count;
    layout.name = std::string_view(c, colon - c);
    return true;
}

size_t payload_size(const std::vector<char> &types) {
    return payload_size(types.size(), std::count(types.begin(), types.end(), '?'));
}

} // namespace telemetry
