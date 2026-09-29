#pragma once

#include "../compilation/variable.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Telemetry frames: a frame is the state of a list of fields from one step, sent as the text line "~<base64(body)>".
// body = id | seq | millis[4] | payload | crc16[2]; the payload holds the numeric fields in definition order
// (little-endian) and then the bools as bits. A layout line "__LAYOUT__v1 <frame>.<index>/<count> <name>:<type>" per
// field tells readers how to decode it; <count> lets them notice a lost last line. Types: f float32, i int32, ? bit.
class Module;

namespace telemetry {

constexpr int FORMAT_VERSION = 1;
constexpr size_t HEADER_SIZE = 6; // id seq millis[4]
constexpr size_t CRC_SIZE = 2;
constexpr size_t MAX_PAYLOAD = 180; // keeps "~" + base64 of the body within one bus message
constexpr size_t MAX_BODY = HEADER_SIZE + MAX_PAYLOAD + CRC_SIZE;
constexpr size_t MAX_LINE = 1 + 4 * ((MAX_BODY + 2) / 3) + 1; // "~", base64, terminator
constexpr char FRAME_PREFIX = '~';
constexpr char LAYOUT_PREFIX[] = "__LAYOUT__";
constexpr size_t LAYOUT_PREFIX_LENGTH = sizeof(LAYOUT_PREFIX) - 1;

struct Field {
    ConstVariable_ptr variable;
    const Module *module;   // nullptr for a global variable
    const std::string *key; // the property's key in the module, or the variable's key in Global::variables
    char type;
};

char type_for(const ConstVariable_ptr &variable);
size_t field_size(char type);
size_t payload_size(const std::vector<Field> &fields);

uint16_t crc16(const uint8_t *data, size_t length);

// the fields' current values, numeric ones first, then the bools as bits; returns the payload length
size_t pack(const std::vector<Field> &fields, uint8_t *payload, size_t capacity);
size_t build_body(uint8_t id, uint8_t seq, uint32_t millis, const uint8_t *payload, size_t length, uint8_t *body);
// "~" + base64(body), terminated; returns the length without the terminator, 0 if it does not fit
size_t encode_line(const uint8_t *body, size_t length, char *line, size_t capacity);
// a "~<base64>" line (without "@xx") with a valid CRC; returns the body length, 0 if it is not a frame
size_t decode_line(const char *line, size_t length, uint8_t *body, size_t capacity);

int format_layout(char *buffer, size_t capacity, uint8_t frame_id, size_t index, size_t count, const std::string &name, char type);

struct LayoutLine {
    int version;
    uint8_t frame_id;
    size_t index;
    size_t count; // number of fields in the frame, 0 if the line does not say
    std::string name;
    char type;
};
bool parse_layout(const char *line, size_t length, LayoutLine &layout);

// what a reader knows about one sender's frames: the type of each field by index (0: index not known yet); names are
// resolved by the listeners when a layout line arrives and not kept
class Layout {
public:
    int version = 0;
    std::map<uint8_t, std::vector<char>> frames;

    bool set(const LayoutLine &line); // true if something changed
    void clear();
    // payload length of a frame whose indices are all known, -1 otherwise
    int expected_payload(uint8_t frame_id) const;
};

// where a field's value sits in the payload and which local variable receives it
struct Slot {
    Variable *variable;
    char type;
    uint8_t mask;    // bit within the byte for a bool
    uint16_t offset; // byte offset in the payload
};

// slots of one frame from its field types and the local variable per index (nullptr: not mapped); empty until complete
std::vector<Slot> map_frame(const std::vector<char> &types, const std::vector<Variable *> &variables);
void apply(const std::vector<Slot> &slots, const uint8_t *payload);
bool type_matches(const Variable &variable, char type);

} // namespace telemetry
