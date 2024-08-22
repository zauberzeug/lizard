#include "canopen.h"

uint32_t wrap_cob_id(CobFunction function, uint8_t node_id) {
    return (function << (11 - 4)) | (node_id);
}

void unwrap_cob_id(uint32_t id, uint8_t &function_out, uint8_t &node_id_out) {
    function_out = (id >> (11 - 4)) & 0xF;
    node_id_out = id & 0x7F;
}

uint32_t make_mapping_entry(uint16_t index, uint8_t sub, uint8_t size) {
    return index << 16 | sub << 8 | size;
}

int32_t demarshal_i32(const uint8_t *const data) {
    uint32_t value_u = demarshal_unsigned<uint32_t>(data);
    return *reinterpret_cast<int32_t *>(&value_u);
}

void marshal_i32(const int32_t value, uint8_t *const data) {
    const uint32_t value_u = *reinterpret_cast<const uint32_t *>(&value);
    marshal_unsigned(value_u, data);
}

void marshal_index(uint16_t index, uint8_t sub, uint8_t *const data) {
    data[0] = index & 0xFF; /* idx low */
    data[1] = index >> 8;   /* idx high */
    data[2] = sub;          /* idx sub */
}

uint8_t check_node_id(int64_t id) {
    if (id < 1 || id > 127) {
        throw std::runtime_error("Invalid CanOpen node id: " + std::to_string(id) + ". Must be in range 1-127");
    }

    return static_cast<uint8_t>(id);
}
