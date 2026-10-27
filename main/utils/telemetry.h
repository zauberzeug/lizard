#pragma once

#include "../compilation/expression.h"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

class SerialBus;

// Binary telemetry frames: each frame is sent as one "~<base64>@xx" line and described by "__LAYOUT__v1" lines.
namespace telemetry {

constexpr char FRAME_PREFIX = '~';
constexpr char LAYOUT_PREFIX[] = "__LAYOUT__";
constexpr int LAYOUT_VERSION = 1;
constexpr size_t HEADER_SIZE = 6; // frame ID, sequence number, millis
constexpr size_t MAX_PAYLOAD = 180;
constexpr size_t CRC_SIZE = 2;
constexpr size_t MAX_BODY = HEADER_SIZE + MAX_PAYLOAD + CRC_SIZE;

void add_frame(const std::vector<ConstExpression_ptr> &arguments);
void announce();
void clear();
void step(); // at the end of each main loop cycle

// a node's own frames go to the coordinator of the first of these buses that is polled, until then to the console
void add_bus(SerialBus *bus);
void remove_bus(SerialBus *bus);

// the body of a "~<base64>" line with a valid CRC; returns its length, 0 if the line is no valid frame
size_t decode_frame(const char *line, size_t length, uint8_t *body);

struct LayoutLine {
    int version;
    uint8_t frame_id;
    size_t index;
    size_t count;
    std::string_view name; // points into the parsed line
    char type;
};
// false if the line is malformed; the fields after the version are only parsed for version 1
bool parse_layout(const char *line, LayoutLine &layout);
size_t payload_size(const std::vector<char> &types); // from the type letters of all fields of a frame

} // namespace telemetry
