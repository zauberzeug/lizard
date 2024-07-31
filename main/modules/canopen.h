#pragma once

#include <cassert>
#include <cinttypes>
#include <stdexcept>

enum CobFunction {
    COB_SYNC_EMCY = 0x1,
    COB_TPDO1 = 0x3,
    COB_RPDO1 = 0x4,
    COB_TPDO2 = 0x5,
    COB_RPDO2 = 0x6,
    COB_TPDO3 = 0x7,
    COB_RPDO3 = 0x8,
    COB_TPDO4 = 0x9,
    COB_RPDO4 = 0xA,
    COB_SDO_SERVER2CLIENT = 0xB,
    COB_SDO_CLIENT2SERVER = 0xC,
    COB_HEARTBEAT = 0xE,
};

enum NmtStateChange {
    STATE_CHANGE_OPERATIONAL = 0x1,
    STATE_CHANGE_PREOPERATIONAL = 0x80,
    STATE_CHANGE_RESET_NODE = 0x81,
    STATE_CHANGE_RESET_COM = 0x82,
};

enum OpModeCode {
    OP_MODE_NONE = 0,
    OP_MODE_PROFILE_POSITION = 1,
    OP_MODE_VELOCITY = 2,
    OP_MODE_PROFILE_VELOCITY = 3,
    OP_MODE_TORQUE_PROFILE = 4,
    OP_MODE_HOMING = 6,
    OP_MODE_INTERPOLATED_POSITION = 7,
};

enum HeartbeatStateCode {
    Booting = 0x00,
    Preoperational = 0x7F,
    Operational = 0x05,
    Stopped = 0x04
};

enum InitState {
    WaitingForPreoperational, // No preop HB received yet
    WaitingForSdoWrites,      // preop HB received, SDOs written
    WaitingForOperational,    // NMT preop -> op transition requested
    InitDone,                 // node reports op state
};

enum ServerCommandSpecifier {
    ExpeditedReadData = 2,
    ExpeditedWriteSuccess = 3,
    WriteFailure = 4,
};

enum SdoWriteFailureReason {
    NonExistantObject = 0x06020000,
    SizeMismatch = 0x06070010,
};

uint32_t wrap_cob_id(CobFunction function, uint8_t node_id);

void unwrap_cob_id(uint32_t id, uint8_t &function_out, uint8_t &node_id_out);

#define CONTROL_WORD_U16 0x6040
#define STATUS_WORD_U16 0x6041
#define OP_MODE_U8 0x6060
#define OP_MODE_DISP_U16 0x6061

static constexpr uint16_t rpdo_com_param_index(uint8_t rpdo) {
    assert(rpdo >= 1 && rpdo <= 4); // device may support more, but don't rely on it
    return 0x1400 + rpdo - 1;       // PDO numbering sadly starts at 1
}

static constexpr uint16_t rpdo_mappings_index(uint8_t rpdo) {
    assert(rpdo >= 1 && rpdo <= 4);
    return 0x1600 + rpdo - 1;
}

static constexpr CobFunction rpdo_func(uint8_t rpdo) {
    assert(rpdo >= 1 && rpdo <= 4);
    constexpr CobFunction idx_to_func[] = {COB_RPDO1, COB_RPDO2, COB_RPDO3, COB_RPDO4};

    return idx_to_func[rpdo - 1];
}

uint32_t make_mapping_entry(uint16_t index, uint8_t sub, uint8_t size);

template <typename U>
static U demarshal_unsigned(const uint8_t *const data) {
    U value = 0;
    for (std::size_t i = 0; i < sizeof(U); ++i) {
        value |= data[i] << 8 * i;
    }

    return value;
}

int32_t demarshal_i32(const uint8_t *const data);

template <typename U>
static void marshal_unsigned(const U value, uint8_t *const data) {
    for (std::size_t i = 0; i < sizeof(U); ++i) {
        data[i] = (value >> (8 * i)) & 0xFF;
    }
}

void marshal_i32(const int32_t value, uint8_t *const data);

static constexpr uint8_t sdo_write_u8_header = (0x1 /*write*/ << (8 - 3)) | (3 << 2 /*8bit*/) | (1 << 1 /*expedited*/) | (1 /*size indicated*/);
static constexpr uint8_t sdo_write_u16_header = (0x1 /*write*/ << (8 - 3)) | (2 << 2 /*16bit*/) | (1 << 1 /*expedited*/) | (1 /*size indicated*/);
static constexpr uint8_t sdo_write_u32_header = (0x1 /*write*/ << (8 - 3)) | (1 << 1 /*expedited*/) | (1 /*size indicated*/);
static constexpr uint8_t sdo_read_header = (0x2 /* read */ << (8 - 3));

void marshal_index(uint16_t index, uint8_t sub, uint8_t *const data);

uint8_t check_node_id(int64_t id);
