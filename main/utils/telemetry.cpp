#include "telemetry.h"
#include "mbedtls/base64.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace telemetry {

char type_for(const ConstVariable_ptr &variable, const bool compact) {
    switch (variable->type) {
    case boolean:
        return '?';
    case integer:
        return 'i';
    case number:
        return compact ? 'e' : 'f';
    default:
        throw std::runtime_error("telemetry fields must be bool, int or float");
    }
}

size_t field_size(const char type) {
    switch (type) {
    case 'f':
    case 'i':
        return 4;
    case 'e':
        return 2;
    default:
        return 0; // bits are counted separately
    }
}

size_t payload_size(const std::vector<Field> &fields) {
    size_t bytes = 0;
    size_t bits = 0;
    for (auto const &field : fields) {
        bytes += field_size(field.type);
        bits += field.type == '?' ? 1 : 0;
    }
    return bytes + (bits + 7) / 8;
}

// CRC-16/CCITT-FALSE
uint16_t bitwise_crc16(const uint8_t *data, const size_t length) {
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < length; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
        }
    }
    return crc;
}

struct CrcTable {
    uint16_t values[256];
    constexpr CrcTable() : values() {
        for (int byte = 0; byte < 256; ++byte) {
            uint16_t crc = byte << 8;
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
            }
            values[byte] = crc;
        }
    }
};
static constexpr CrcTable CRC_TABLE;

uint16_t crc16(const uint8_t *data, const size_t length) {
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < length; ++i) {
        crc = (crc << 8) ^ CRC_TABLE.values[(crc >> 8) ^ data[i]];
    }
    return crc;
}

static constexpr char BASE64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

struct DecodeTable {
    int8_t values[256];
    constexpr DecodeTable() : values() {
        for (int i = 0; i < 256; ++i) {
            values[i] = -1;
        }
        for (int i = 0; i < 64; ++i) {
            values[static_cast<uint8_t>(BASE64[i])] = i;
        }
    }
};
static constexpr DecodeTable DECODE_TABLE;

uint16_t float_to_half(const float value) {
    uint32_t f;
    memcpy(&f, &value, 4);
    const uint32_t sign = (f >> 16) & 0x8000;
    const int32_t exponent = static_cast<int32_t>((f >> 23) & 0xff) - 127 + 15;
    uint32_t mantissa = f & 0x7fffff;
    if (((f >> 23) & 0xff) == 0xff) { // inf or nan
        return sign | 0x7c00 | (mantissa ? 0x200 : 0);
    }
    if (exponent >= 0x1f) {
        return sign | 0x7c00; // overflow to inf
    }
    if (exponent <= 0) {
        if (exponent < -10) {
            return sign; // underflow to zero
        }
        mantissa |= 0x800000;
        const int shift = 14 - exponent;
        uint32_t half = mantissa >> shift;
        if ((mantissa >> (shift - 1)) & 1) {
            ++half; // round half up
        }
        return sign | half;
    }
    uint32_t half = sign | (exponent << 10) | (mantissa >> 13);
    if (mantissa & 0x1000) {
        ++half; // round half up, may carry into the exponent
    }
    return half;
}

float half_to_float(const uint16_t half) {
    const uint32_t sign = static_cast<uint32_t>(half & 0x8000) << 16;
    const uint32_t exponent = (half >> 10) & 0x1f;
    uint32_t mantissa = half & 0x3ff;
    uint32_t f;
    if (exponent == 0) {
        if (mantissa == 0) {
            f = sign;
        } else {
            int e = -1;
            do {
                ++e;
                mantissa <<= 1;
            } while ((mantissa & 0x400) == 0);
            f = sign | ((127 - 15 - e) << 23) | ((mantissa & 0x3ff) << 13);
        }
    } else if (exponent == 0x1f) {
        f = sign | 0x7f800000 | (mantissa << 13);
    } else {
        f = sign | ((exponent - 15 + 127) << 23) | (mantissa << 13);
    }
    float value;
    memcpy(&value, &f, 4);
    return value;
}

size_t pack(const std::vector<Field> &fields, uint8_t *payload, const size_t capacity) {
    if (payload_size(fields) > capacity) {
        throw std::runtime_error("telemetry payload exceeds its buffer");
    }
    size_t pos = 0;
    size_t bits = 0;
    for (auto const &field : fields) {
        switch (field.type) {
        case 'f': {
            const float value = static_cast<float>(field.variable->number_value());
            memcpy(&payload[pos], &value, 4);
            pos += 4;
            break;
        }
        case 'e': {
            const uint16_t value = float_to_half(static_cast<float>(field.variable->number_value()));
            memcpy(&payload[pos], &value, 2);
            pos += 2;
            break;
        }
        case 'i': {
            const int64_t raw = field.variable->integer_value();
            const int32_t value = static_cast<int32_t>(std::max<int64_t>(std::numeric_limits<int32_t>::min(),
                                                                         std::min<int64_t>(std::numeric_limits<int32_t>::max(), raw)));
            memcpy(&payload[pos], &value, 4);
            pos += 4;
            break;
        }
        case '?':
            ++bits;
            break;
        }
    }
    memset(&payload[pos], 0, (bits + 7) / 8);
    size_t bit = 0;
    for (auto const &field : fields) {
        if (field.type == '?') {
            if (field.variable->boolean_value()) {
                payload[pos + bit / 8] |= 1 << (bit % 8);
            }
            ++bit;
        }
    }
    return pos + (bits + 7) / 8;
}

size_t build_body(const uint8_t id, const uint8_t seq, const uint32_t millis, const uint8_t *payload, const size_t length, uint8_t *body) {
    body[0] = id;
    body[1] = seq;
    memcpy(&body[2], &millis, 4);
    memcpy(&body[HEADER_SIZE], payload, length);
    const uint16_t crc = crc16(body, HEADER_SIZE + length);
    memcpy(&body[HEADER_SIZE + length], &crc, 2);
    return HEADER_SIZE + length + CRC_SIZE;
}

size_t encode_line(const uint8_t *body, const size_t length, char *line, const size_t capacity) {
    const size_t needed = 1 + 4 * ((length + 2) / 3) + 1;
    if (capacity < needed) {
        return 0;
    }
    size_t pos = 0;
    line[pos++] = FRAME_PREFIX;
    for (size_t i = 0; i < length; i += 3) {
        const uint32_t v = body[i] << 16 | (i + 1 < length ? body[i + 1] << 8 : 0) | (i + 2 < length ? body[i + 2] : 0);
        line[pos++] = BASE64[v >> 18 & 0x3f];
        line[pos++] = BASE64[v >> 12 & 0x3f];
        line[pos++] = i + 1 < length ? BASE64[v >> 6 & 0x3f] : '=';
        line[pos++] = i + 2 < length ? BASE64[v & 0x3f] : '=';
    }
    line[pos] = '\0';
    return pos;
}

size_t decode_line(const char *line, const size_t length, uint8_t *body, const size_t capacity) {
    if (length < 5 || line[0] != FRAME_PREFIX || (length - 1) % 4 != 0) {
        return 0;
    }
    size_t out = 0;
    for (size_t i = 1; i < length; i += 4) {
        const bool last = i + 4 == length;
        const bool pad2 = last && line[i + 2] == '=';
        const bool pad3 = last && line[i + 3] == '=';
        const int8_t a = DECODE_TABLE.values[static_cast<uint8_t>(line[i])];
        const int8_t b = DECODE_TABLE.values[static_cast<uint8_t>(line[i + 1])];
        const int8_t c = pad2 ? 0 : DECODE_TABLE.values[static_cast<uint8_t>(line[i + 2])];
        const int8_t d = pad3 ? 0 : DECODE_TABLE.values[static_cast<uint8_t>(line[i + 3])];
        if (a < 0 || b < 0 || c < 0 || d < 0 || (pad2 && !pad3)) {
            return 0;
        }
        const uint32_t v = a << 18 | b << 12 | c << 6 | d;
        const size_t n = pad2 ? 1 : pad3 ? 2
                                         : 3;
        if (out + n > capacity) {
            return 0;
        }
        body[out++] = v >> 16;
        if (n > 1) {
            body[out++] = v >> 8;
        }
        if (n > 2) {
            body[out++] = v;
        }
    }
    if (out < HEADER_SIZE + CRC_SIZE) {
        return 0;
    }
    uint16_t crc;
    memcpy(&crc, &body[out - CRC_SIZE], 2);
    return crc == crc16(body, out - CRC_SIZE) ? out : 0;
}

size_t mbedtls_encode_line(const uint8_t *body, const size_t length, char *line, const size_t capacity) {
    if (capacity < 2) {
        return 0;
    }
    line[0] = FRAME_PREFIX;
    size_t written = 0;
    if (mbedtls_base64_encode(reinterpret_cast<unsigned char *>(&line[1]), capacity - 1, &written, body, length) != 0) {
        return 0;
    }
    return 1 + written;
}

size_t mbedtls_decode_line(const char *line, const size_t length, uint8_t *body, const size_t capacity) {
    if (length < 2 || line[0] != FRAME_PREFIX || (length - 1) % 4 != 0) {
        return 0;
    }
    size_t decoded = 0;
    if (mbedtls_base64_decode(body, capacity, &decoded, reinterpret_cast<const unsigned char *>(&line[1]), length - 1) != 0) {
        return 0;
    }
    if (decoded < HEADER_SIZE + CRC_SIZE) {
        return 0;
    }
    uint16_t crc;
    memcpy(&crc, &body[decoded - CRC_SIZE], 2);
    return crc == bitwise_crc16(body, decoded - CRC_SIZE) ? decoded : 0;
}

size_t cobs_encode(const uint8_t *input, const size_t length, uint8_t *output) {
    size_t read = 0, write = 1, code_pos = 0;
    uint8_t code = 1;
    while (read < length) {
        if (input[read] == 0) {
            output[code_pos] = code;
            code_pos = write++;
            code = 1;
        } else {
            output[write++] = input[read];
            if (++code == 0xff) {
                output[code_pos] = code;
                code_pos = write++;
                code = 1;
            }
        }
        ++read;
    }
    output[code_pos] = code;
    return write;
}

static bool needs_escape(const uint8_t byte, const bool line_only) {
    return byte == 0x0a || byte == 0x0d || byte == 0x7d || (!line_only && (byte == 0x00 || byte == 0x09 || byte == 0x20));
}

size_t stuff(const uint8_t *body, const size_t length, char *output, const size_t capacity, const bool line_only) {
    size_t pos = 0;
    output[pos++] = 0x01;
    for (size_t i = 0; i < length; ++i) {
        if (pos + 2 >= capacity) {
            return 0;
        }
        if (needs_escape(body[i], line_only)) {
            output[pos++] = 0x7d;
            output[pos++] = body[i] ^ 0x50;
        } else {
            output[pos++] = body[i];
        }
    }
    output[pos] = '\0';
    return pos;
}

size_t unstuff(const char *input, const size_t length, uint8_t *body, const size_t capacity) {
    if (length < 1 || static_cast<uint8_t>(input[0]) != 0x01) {
        return 0;
    }
    size_t pos = 0;
    for (size_t i = 1; i < length; ++i) {
        uint8_t byte = input[i];
        if (byte == 0x7d) {
            if (++i >= length) {
                return 0;
            }
            byte = input[i] ^ 0x50;
        }
        if (pos >= capacity) {
            return 0;
        }
        body[pos++] = byte;
    }
    return pos;
}

int format_layout(char *buffer, const size_t capacity, const uint8_t frame_id, const size_t index, const std::string &name, const char type) {
    return std::snprintf(buffer, capacity, "%sv%d %u.%u %s:%c", LAYOUT_PREFIX, FORMAT_VERSION, frame_id,
                         static_cast<unsigned>(index), name.c_str(), type);
}

bool parse_layout(const char *line, const size_t length, LayoutLine &layout) {
    if (length <= LAYOUT_PREFIX_LENGTH + 1 || strncmp(line, LAYOUT_PREFIX, LAYOUT_PREFIX_LENGTH) != 0 ||
        line[LAYOUT_PREFIX_LENGTH] != 'v') {
        return false;
    }
    const std::string text(line + LAYOUT_PREFIX_LENGTH + 1, length - LAYOUT_PREFIX_LENGTH - 1);
    char *end = nullptr;
    const long version = strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != ' ') {
        return false;
    }
    const long frame_id = strtol(end + 1, &end, 10);
    if (*end != '.' || frame_id < 0 || frame_id > 255) {
        return false;
    }
    const long index = strtol(end + 1, &end, 10);
    if (*end != ' ' || index < 0 || index > 255) {
        return false;
    }
    const std::string field(end + 1);
    const size_t colon = field.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 2 != field.size()) {
        return false;
    }
    layout.version = static_cast<int>(version);
    layout.frame_id = static_cast<uint8_t>(frame_id);
    layout.index = static_cast<size_t>(index);
    layout.name = field.substr(0, colon);
    layout.type = field[colon + 1];
    return layout.type == 'f' || layout.type == 'i' || layout.type == '?' || layout.type == 'e';
}

bool Layout::set(const LayoutLine &line) {
    bool changed = line.version != this->version;
    this->version = line.version;
    std::vector<char> &types = this->frames[line.frame_id];
    if (types.size() <= line.index) {
        types.resize(line.index + 1, 0);
        changed = true;
    }
    if (types[line.index] != line.type) {
        types[line.index] = line.type;
        changed = true;
    }
    return changed;
}

void Layout::clear() {
    this->frames.clear();
}

int Layout::expected_payload(const uint8_t frame_id) const {
    const auto it = this->frames.find(frame_id);
    if (it == this->frames.end()) {
        return -1;
    }
    size_t bytes = 0;
    size_t bits = 0;
    for (const char type : it->second) {
        if (type == 0) {
            return -1;
        }
        bytes += field_size(type);
        bits += type == '?' ? 1 : 0;
    }
    return static_cast<int>(bytes + (bits + 7) / 8);
}

bool type_matches(const Variable &variable, const char type) {
    switch (type) {
    case 'f':
    case 'e':
        return variable.type == number;
    case 'i':
        return variable.type == integer;
    case '?':
        return variable.type == boolean;
    default:
        return false;
    }
}

std::vector<Slot> map_frame(const std::vector<char> &types, const std::vector<Variable *> &variables) {
    size_t numeric = 0;
    for (const char type : types) {
        if (type == 0) {
            return {};
        }
        numeric += field_size(type);
    }
    std::vector<Slot> slots;
    size_t offset = 0;
    size_t bit = 0;
    for (size_t index = 0; index < types.size(); ++index) {
        Variable *const variable = index < variables.size() ? variables[index] : nullptr;
        if (types[index] == '?') {
            if (variable) {
                slots.push_back({variable, '?', static_cast<uint8_t>(1 << (bit % 8)), static_cast<uint16_t>(numeric + bit / 8)});
            }
            ++bit;
        } else {
            if (variable) {
                slots.push_back({variable, types[index], 0, static_cast<uint16_t>(offset)});
            }
            offset += field_size(types[index]);
        }
    }
    return slots;
}

void apply(const std::vector<Slot> &slots, const uint8_t *payload) {
    for (auto const &slot : slots) {
        switch (slot.type) {
        case 'f': {
            float value;
            memcpy(&value, &payload[slot.offset], 4);
            slot.variable->set_number_value(value);
            break;
        }
        case 'e': {
            uint16_t value;
            memcpy(&value, &payload[slot.offset], 2);
            slot.variable->set_number_value(half_to_float(value));
            break;
        }
        case 'i': {
            int32_t value;
            memcpy(&value, &payload[slot.offset], 4);
            slot.variable->set_integer_value(value);
            break;
        }
        case '?':
            slot.variable->set_boolean_value((payload[slot.offset] & slot.mask) != 0);
            break;
        }
    }
}

} // namespace telemetry
