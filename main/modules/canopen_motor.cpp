#include "canopen_motor.h"
#include "timing.h"
#include "uart.h"
#include <cassert>
#include <cinttypes>
#include <esp_timer.h>
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

enum ServerCommandSpecifier {
    ExpeditedReadData = 2,
    ExpeditedWriteSuccess = 3,
    WriteFailure = 4,
};

enum SdoWriteFailureReason {
    NonExistantObject = 0x06020000,
    SizeMismatch = 0x06070010,
};

static uint32_t wrap_cob_id(CobFunction function, uint8_t node_id) {
    return (function << (11 - 4)) | (node_id);
}

static void unwrap_cob_id(uint32_t id, uint8_t &function_out, uint8_t &node_id_out) {
    function_out = (id >> (11 - 4)) & 0xF;
    node_id_out = id & 0x7F;
}

#define CONTROL_WORD_U16 0x6040
#define STATUS_WORD_U16 0x6041
#define OP_MODE_U8 0x6060
#define OP_MODE_DISP_U16 0x6061

#define DIGITAL_INPUTS_U32 0x60FD

#define TARGET_POSITION_I32 0x607A
#define ACTUAL_POSITION_I32 0x6064
#define ACTUAL_VELOCITY_I32 0x606C

#define MAX_PROFILE_VELOCITY_U32 0x607F
#define PROFILE_VELOCITY_U32 0x6081
#define PROFILE_ACCELERATION_U32 0x6083
#define PROFILE_DECELERATION_U32 0x6084
#define QUICK_STOP_DECELERATION_U32 0x6085
#define TARGET_VELOCITY_RO_U16 0x60FF // RW, I32 in DSP402

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

static uint32_t make_mapping_entry(uint16_t index, uint8_t sub, uint8_t size) {
    return index << 16 | sub << 8 | size;
}

template <typename U>
static U demarshal_unsigned(const uint8_t *const data) {
    U value = 0;
    for (std::size_t i = 0; i < sizeof(U); ++i) {
        value |= data[i] << 8 * i;
    }

    return value;
}

static int32_t demarshal_i32(const uint8_t *const data) {
    uint32_t value_u = demarshal_unsigned<uint32_t>(data);
    return *reinterpret_cast<int32_t *>(&value_u);
}

template <typename U>
static void marshal_unsigned(const U value, uint8_t *const data) {
    for (std::size_t i = 0; i < sizeof(U); ++i) {
        data[i] = (value >> (8 * i)) & 0xFF;
    }
}

static void marshal_i32(const int32_t value, uint8_t *const data) {
    const uint32_t value_u = *reinterpret_cast<const uint32_t *>(&value);
    marshal_unsigned(value_u, data);
}

static constexpr uint8_t sdo_write_u8_header = (0x1 /*write*/ << (8 - 3)) | (3 << 2 /*8bit*/) | (1 << 1 /*expedited*/) | (1 /*size indicated*/);
static constexpr uint8_t sdo_write_u16_header = (0x1 /*write*/ << (8 - 3)) | (2 << 2 /*16bit*/) | (1 << 1 /*expedited*/) | (1 /*size indicated*/);
static constexpr uint8_t sdo_write_u32_header = (0x1 /*write*/ << (8 - 3)) | (1 << 1 /*expedited*/) | (1 /*size indicated*/);
static constexpr uint8_t sdo_read_header = (0x2 /* read */ << (8 - 3));

static void marshal_index(uint16_t index, uint8_t sub, uint8_t *const data) {
    data[0] = index & 0xFF; /* idx low */
    data[1] = index >> 8;   /* idx high */
    data[2] = sub;          /* idx sub */
}

static uint16_t build_ctrl_base_word(uint16_t switch_on, uint16_t ena_volate, uint16_t quick_stop, uint16_t ena_op, uint16_t halt) {
    /* ena_op is enough to turn JMC motor on/off */
    return (switch_on) | (ena_volate << 1) | (quick_stop << 2) | (ena_op << 3) | (halt << 8);
}

static uint16_t build_ctrl_pos_prof_word(uint16_t new_set_point, uint16_t change_set_immed, uint16_t rel_pos) {
    return (new_set_point << 4) | (change_set_immed << 5) | (rel_pos << 6);
}

static uint8_t check_node_id(int64_t id) {
    if (id < 1 || id > 127) {
        throw std::runtime_error("Invalid CanOpen node id: " + std::to_string(id) + ". Must be in range 1-127");
    }

    return static_cast<uint8_t>(id);
}

static const std::string PROP_INITIALIZED{"initialized"};
static const std::string PROP_PENDING_READS{"pending_sdo_reads"};
static const std::string PROP_PENDING_WRITES{"pending_sdo_writes"};
static const std::string PROP_HEARTBEAT{"last_heartbeat"};
static const std::string PROP_301_STATE{"raw_state"};
static const std::string PROP_301_STATE_BOOTING{"is_booting"};
static const std::string PROP_301_STATE_PREOP{"is_preoperational"};
static const std::string PROP_301_STATE_OP{"is_operational"};
static const std::string PROP_OFFSET{"position_offset"};
static const std::string PROP_POSITION{"actual_position"};
static const std::string PROP_VELOCITY{"actual_velocity"};
static const std::string PROP_402_OP_ENA{"status_enabled"};
static const std::string PROP_402_FAULT{"status_fault"};
static const std::string PROP_TARGET_REACHED{"status_target_reached"};
static const std::string PROP_PP_SET_POINT_ACK{"pp_set_point_acknowledge"};
static const std::string PROP_PV_IS_MOVING{"pv_is_moving"};
static const std::string PROP_CTRL_ENA_OP{"ctrl_enable"};
static const std::string PROP_CTRL_HALT{"ctrl_halt"};

CanOpenMotor::CanOpenMotor(const std::string &name, Can_ptr can, int64_t node_id)
    : Module(canopen_motor, name), can(can), node_id(check_node_id(node_id)),
      current_op_mode_disp(OP_MODE_NONE), current_op_mode(OP_MODE_NONE) {
    this->properties[PROP_INITIALIZED] = std::make_shared<BooleanVariable>(false);
    this->properties[PROP_PENDING_READS] = std::make_shared<IntegerVariable>(0);
    this->properties[PROP_PENDING_WRITES] = std::make_shared<IntegerVariable>(0);
    this->properties[PROP_HEARTBEAT] = std::make_shared<IntegerVariable>(-1);
    this->properties[PROP_301_STATE] = std::make_shared<IntegerVariable>(-1);
    this->properties[PROP_301_STATE_BOOTING] = std::make_shared<BooleanVariable>(false);
    this->properties[PROP_301_STATE_PREOP] = std::make_shared<BooleanVariable>(false);
    this->properties[PROP_301_STATE_OP] = std::make_shared<BooleanVariable>(false);
    this->properties[PROP_OFFSET] = std::make_shared<IntegerVariable>(0);
    this->properties[PROP_POSITION] = std::make_shared<IntegerVariable>(0);
    this->properties[PROP_VELOCITY] = std::make_shared<IntegerVariable>(0);
    this->properties[PROP_402_OP_ENA] = std::make_shared<BooleanVariable>(false);
    this->properties[PROP_402_FAULT] = std::make_shared<BooleanVariable>(false);
    this->properties[PROP_TARGET_REACHED] = std::make_shared<BooleanVariable>(false);
    this->properties[PROP_PP_SET_POINT_ACK] = std::make_shared<BooleanVariable>(false);
    this->properties[PROP_PV_IS_MOVING] = std::make_shared<BooleanVariable>(false);
    this->properties[PROP_CTRL_ENA_OP] = std::make_shared<BooleanVariable>(false);
    this->properties[PROP_CTRL_HALT] = std::make_shared<BooleanVariable>(true);
}

void CanOpenMotor::wait_for_sdo_writes(uint32_t timeout_ms) {
    const uint32_t ms_per_sleep = 10;
    uint32_t cycles = timeout_ms / ms_per_sleep;

    for (uint32_t i = 0; i < cycles; ++i) {
        while (this->can->receive())
            ;
        delay(ms_per_sleep);

        if (this->properties[PROP_PENDING_WRITES]->integer_value == 0) {
            return;
        }
    }

    throw std::runtime_error("SDO writes timed out. Aborting.");
}

void CanOpenMotor::enter_position_mode(int velocity) {
    write_od_u8(OP_MODE_U8, 0x00, OP_MODE_PROFILE_POSITION);
    send_target_velocity(velocity);
    /* Take off halt (=brake) for positioning by default */
    this->properties[PROP_CTRL_HALT]->boolean_value = false;
    send_control_word(build_ctrl_word(false));

    current_op_mode = OP_MODE_PROFILE_POSITION;
}

void CanOpenMotor::enter_velocity_mode(int velocity) {
    /* Put in halt for velocity mode since it directly controls motion */
    this->properties[PROP_CTRL_HALT]->boolean_value = true;
    send_control_word(build_ctrl_word(false));
    send_target_velocity(velocity);
    write_od_u8(OP_MODE_U8, 0x00, OP_MODE_PROFILE_VELOCITY);

    current_op_mode = OP_MODE_PROFILE_VELOCITY;
}

void CanOpenMotor::set_profile_acceleration(uint16_t acceleration) {
    write_od_u16(PROFILE_ACCELERATION_U32, 0x00, acceleration);
}

void CanOpenMotor::set_profile_deceleration(uint16_t deceleration) {
    write_od_u16(PROFILE_DECELERATION_U32, 0x00, deceleration);
}

void CanOpenMotor::set_profile_quick_stop_deceleration(uint16_t deceleration) {
    write_od_u16(QUICK_STOP_DECELERATION_U32, 0x00, deceleration);
}

void CanOpenMotor::subscribe_to_can() {
    can->subscribe(wrap_cob_id(COB_HEARTBEAT, node_id), this->shared_from_this());
    can->subscribe(wrap_cob_id(COB_SDO_SERVER2CLIENT, node_id), this->shared_from_this());
    can->subscribe(wrap_cob_id(COB_TPDO1, node_id), this->shared_from_this());
    can->subscribe(wrap_cob_id(COB_TPDO2, node_id), this->shared_from_this());
}

void CanOpenMotor::step() {
}

void CanOpenMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (!this->properties[PROP_INITIALIZED]->boolean_value) {
        throw std::runtime_error("CanOpenMotor: Not initialized!");
    }

    if (method_name == "enter_pp_mode") {
        expect(arguments, 1, integer);
        int64_t velocity = arguments[0]->evaluate_integer();
        enter_position_mode(velocity);
    } else if (method_name == "enter_pv_mode") {
        expect(arguments, 1, integer);
        int64_t velocity = arguments[0]->evaluate_integer();
        enter_velocity_mode(velocity);
    } else if (method_name == "set_target_position") {
        expect(arguments, 1, integer);
        int32_t target_position = arguments[0]->evaluate_integer();
        int32_t offset = this->properties[PROP_OFFSET]->integer_value;
        send_target_position(target_position + offset);
    } else if (method_name == "commit_target_position") {
        expect(arguments, 0);
        /* toggle new set point bit in control word */
        send_control_word(build_ctrl_word(true));
        send_control_word(build_ctrl_word(false));
    } else if (method_name == "set_target_velocity") {
        expect(arguments, 1, integer);
        int32_t target_velocity = arguments[0]->evaluate_integer();
        send_target_velocity(target_velocity);
    } else if (method_name == "set_ctrl_halt") {
        expect(arguments, 1, boolean);
        this->properties[PROP_CTRL_HALT]->boolean_value = arguments[0]->evaluate_boolean();
        send_control_word(build_ctrl_word(false));
    } else if (method_name == "set_ctrl_enable") {
        expect(arguments, 1, boolean);
        this->properties[PROP_CTRL_ENA_OP]->boolean_value = arguments[0]->evaluate_boolean();
        send_control_word(build_ctrl_word(false));
    } else if (method_name == "reset_fault") {
        expect(arguments, 0);
        /* implicitly set halt bit so we don't start moving immediately after the fault is cleared */
        this->properties[PROP_CTRL_HALT]->boolean_value = true;
        uint16_t ctrl_word = build_ctrl_word(false);
        /* set fault reset bit */
        ctrl_word |= (1 << 7);
        send_control_word(ctrl_word);
        /* and clear it */
        ctrl_word &= ~(1 << 7);
        send_control_word(ctrl_word);
    } else if (method_name == "sdo_read") {
        expect(arguments, 1, integer);
        uint16_t index = arguments[0]->evaluate_integer();
        sdo_read(index, 0);
    } else if (method_name == "set_profile_acceleration") {
        expect(arguments, 1, integer);
        uint32_t acceleration = arguments[0]->evaluate_integer();
        set_profile_acceleration(acceleration);
    } else if (method_name == "set_profile_deceleration") {
        expect(arguments, 1, integer);
        uint32_t deceleration = arguments[0]->evaluate_integer();
        set_profile_deceleration(deceleration);
    } else if (method_name == "set_profile_quick_stop_deceleration") {
        expect(arguments, 1, integer);
        uint32_t deceleration = arguments[0]->evaluate_integer();
        set_profile_quick_stop_deceleration(deceleration);
    } else {
        Module::call(method_name, arguments);
    }
}

void CanOpenMotor::transition_preoperational() {
    uint8_t data[2]{STATE_CHANGE_PREOPERATIONAL, this->node_id};
    /* COB-ID 0 = NMT state transition */
    this->can->send(0, data, false, sizeof(data));
}

void CanOpenMotor::transition_operational() {
    uint8_t data[2]{STATE_CHANGE_OPERATIONAL, this->node_id};
    /* COB-ID 0 = NMT state transition */
    this->can->send(0, data, false, sizeof(data));
}

void CanOpenMotor::write_od_u8(uint16_t index, uint8_t sub, uint8_t value) {
    uint8_t data[8];
    data[0] = sdo_write_u8_header;
    marshal_index(index, sub, &data[1]);
    marshal_unsigned(value, &data[4]);

    can->send(wrap_cob_id(COB_SDO_CLIENT2SERVER, node_id), data);
    this->properties[PROP_PENDING_WRITES]->integer_value++;
    wait_for_sdo_writes(100);
}

void CanOpenMotor::write_od_u16(uint16_t index, uint8_t sub, uint16_t value) {
    uint8_t data[8];
    data[0] = sdo_write_u16_header;
    marshal_index(index, sub, &data[1]);
    marshal_unsigned(value, &data[4]);

    can->send(wrap_cob_id(COB_SDO_CLIENT2SERVER, node_id), data);
    this->properties[PROP_PENDING_WRITES]->integer_value++;
    wait_for_sdo_writes(100);
}

void CanOpenMotor::write_od_u32(uint16_t index, uint8_t sub, uint32_t value) {
    uint8_t data[8];
    data[0] = sdo_write_u32_header;
    marshal_index(index, sub, &data[1]);
    marshal_unsigned(value, &data[4]);

    can->send(wrap_cob_id(COB_SDO_CLIENT2SERVER, node_id), data);
    this->properties[PROP_PENDING_WRITES]->integer_value++;
    wait_for_sdo_writes(100);
}

void CanOpenMotor::write_od_i32(uint16_t index, uint8_t sub, int32_t value) {
    uint8_t data[8];
    data[0] = sdo_write_u32_header;
    marshal_index(index, sub, &data[1]);
    marshal_i32(value, &data[4]);

    can->send(wrap_cob_id(COB_SDO_CLIENT2SERVER, node_id), data);
    this->properties[PROP_PENDING_WRITES]->integer_value++;
    wait_for_sdo_writes(100);
}

void CanOpenMotor::sdo_read(uint16_t index, uint8_t sub) {
    uint8_t data[8] = {sdo_read_header};
    marshal_index(index, sub, &data[1]);

    this->can->send(wrap_cob_id(COB_SDO_CLIENT2SERVER, node_id), data);
}

void CanOpenMotor::write_rpdo_mapping(uint32_t *entries, uint8_t entry_count, uint8_t rpdo) {
    assert(rpdo >= 1 && rpdo <= 4);

    /* Disable PDO (set invalid COB-ID) */
    write_od_u32(rpdo_com_param_index(rpdo), 0x01, (uint32_t)-1);
    /* Set mapping count to 0 before filling entries */
    write_od_u8(rpdo_mappings_index(rpdo), 0x00, 0);
    /* Write mappings */
    for (uint8_t i = 0; i < entry_count; ++i) {
        write_od_u32(rpdo_mappings_index(rpdo), i + 1, entries[i]);
    }
    /* Update mapping count to actual value */
    write_od_u8(rpdo_mappings_index(rpdo), 0x00, entry_count);
    write_od_u32(rpdo_com_param_index(rpdo), 0x01, wrap_cob_id(rpdo_func(rpdo), this->node_id));
}

void CanOpenMotor::configure_rpdos() {
    uint32_t mapping = make_mapping_entry(CONTROL_WORD_U16, 0, 16); // array of 1
    write_rpdo_mapping(&mapping, 1, 1);

    mapping = make_mapping_entry(TARGET_POSITION_I32, 0, 32);
    write_rpdo_mapping(&mapping, 1, 2);

    mapping = make_mapping_entry(PROFILE_VELOCITY_U32, 0, 32);
    write_rpdo_mapping(&mapping, 1, 3);
}

void CanOpenMotor::configure_constants() {
    write_od_u8(OP_MODE_U8, 0x00, OP_MODE_NONE);
    write_od_u16(PROFILE_ACCELERATION_U32, 0x00, 400);
    write_od_u16(PROFILE_DECELERATION_U32, 0x00, 400);
    write_od_u16(QUICK_STOP_DECELERATION_U32, 0x00, 2000);
    write_od_u16(CONTROL_WORD_U16, 0x00, build_ctrl_word(false));

    configure_rpdos();
}

void CanOpenMotor::handle_heartbeat(const uint8_t *const data) {
    uint8_t actual_state = data[0];

    this->properties[PROP_HEARTBEAT]->integer_value = esp_timer_get_time();
    this->properties[PROP_301_STATE]->integer_value = actual_state;

    this->properties[PROP_301_STATE_BOOTING]->boolean_value = (actual_state == Booting);
    this->properties[PROP_301_STATE_PREOP]->boolean_value = (actual_state == Preoperational);
    this->properties[PROP_301_STATE_OP]->boolean_value = (actual_state == Operational);

    if (actual_state == Booting) {
        /* Possible reboot, restart initialization */
        init_state = WaitingForPreoperational;
        this->properties[PROP_INITIALIZED]->boolean_value = false;
        return;
    }

    switch (init_state) {
    case WaitingForPreoperational:
        switch (actual_state) {
        case Operational:
            transition_preoperational();
            break;

        case Preoperational:
            configure_constants();
            init_state = WaitingForSdoWrites;
            break;

        case Stopped:
            throw std::runtime_error("CanOpenMotor: Unexpected stopped state");

        default:
            break;
        }
        break;

    case WaitingForSdoWrites:
        switch (actual_state) {
        case Preoperational:
            if (this->properties[PROP_PENDING_WRITES]->integer_value > 0) {
                break;
            }

            transition_operational();
            init_state = WaitingForOperational;
            break;

        default:
            throw std::runtime_error("CanOpenMotor: Unexpected state waiting for SDO writes");
        }
        break;

    case WaitingForOperational:
        switch (actual_state) {
        case Operational:
            init_state = InitDone;
            this->properties[PROP_INITIALIZED]->boolean_value = true;
            break;

        case Preoperational:
            break;

        default:
            throw std::runtime_error("CanOpenMotor: Unexpected state waiting for operational");
        }
        break;

    case InitDone:
        break;
    }
}

void CanOpenMotor::handle_sdo_reply(const uint8_t *const data) {
    uint8_t scs = data[0] >> (8 - 3);
    uint16_t index = data[1] | data[2] << 8;
    uint8_t sub_index = data[3];
    uint32_t value = demarshal_unsigned<uint32_t>(data + 4);

    switch (scs) {
    case ExpeditedReadData:
        echo("Incoming read: [%02X.%01X]: %04X (%d)", index, sub_index, value, *reinterpret_cast<int32_t *>(&value));
        switch (index) {
        case OP_MODE_DISP_U16:
            this->current_op_mode_disp = static_cast<uint16_t>(value);
            break;
        }
        break;

    case ExpeditedWriteSuccess:
        assert(this->properties[PROP_PENDING_WRITES]->integer_value > 0);
        this->properties[PROP_PENDING_WRITES]->integer_value--;
        break;

    case WriteFailure:
        /* A failure still acknowledges the write operation */
        this->properties[PROP_PENDING_WRITES]->integer_value--;

        switch (value) {
        case NonExistantObject:
            echo("Attempting to write non-existant object [%02X.%01X]", index, sub_index);
            break;

        case SizeMismatch:
            echo("Written size for object [%02X.%01X] does not match", index, sub_index);
            break;

        default:
            echo("Unknown error [%04X] attempting to write object [%02X.%01X]", value, index, sub_index);
        }
        break;

    default:
        echo("Unknown server command specifier %u", scs);
    }
}

void CanOpenMotor::handle_tpdo1(const uint8_t *const data) {
    uint16_t status_word = data[0] | data[1] << 8;
    int32_t actual_position = demarshal_i32(data + 2);
    actual_position -= this->properties[PROP_OFFSET]->integer_value;

    process_status_word_generic(status_word);

    if (current_op_mode == OP_MODE_PROFILE_POSITION) {
        process_status_word_pp(status_word);
    } else if (current_op_mode == OP_MODE_PROFILE_VELOCITY) {
        process_status_word_pv(status_word);
    }

    this->properties[PROP_POSITION]->integer_value = actual_position;
}

void CanOpenMotor::handle_tpdo2(const uint8_t *const data) {
    int32_t actual_velocity = demarshal_i32(data);
    this->properties[PROP_VELOCITY]->integer_value = actual_velocity;
}

void CanOpenMotor::process_status_word_generic(const uint16_t status_word) {
    this->properties[PROP_402_OP_ENA]->boolean_value = status_word >> 2 & 1;
    this->properties[PROP_402_FAULT]->boolean_value = status_word >> 3 & 1;
    this->properties[PROP_TARGET_REACHED]->boolean_value = status_word >> 10 & 1;
}

void CanOpenMotor::process_status_word_pp(const uint16_t status_word) {
    this->properties[PROP_PP_SET_POINT_ACK]->boolean_value = status_word >> 12 & 1;
}

void CanOpenMotor::process_status_word_pv(const uint16_t status_word) {
    this->properties[PROP_PV_IS_MOVING]->boolean_value = status_word >> 12 & 1;
}

void CanOpenMotor::send_control_word(uint16_t value) {
    uint8_t data[2];
    marshal_unsigned(value, data);
    this->can->send(wrap_cob_id(COB_RPDO1, this->node_id), data, false, sizeof(data));
}

void CanOpenMotor::send_target_position(int32_t value) {
    uint8_t data[4];
    marshal_i32(value, data);
    this->can->send(wrap_cob_id(COB_RPDO2, this->node_id), data, false, sizeof(data));
}

void CanOpenMotor::send_target_velocity(int32_t value) {
    uint8_t data[4];
    marshal_i32(value, data);
    this->can->send(wrap_cob_id(COB_RPDO3, this->node_id), data, false, sizeof(data));
}

uint16_t CanOpenMotor::build_ctrl_word(bool new_set_point) {
    uint16_t ena_op_bit = this->properties[PROP_CTRL_ENA_OP]->boolean_value ? 1 : 0;
    uint16_t halt_bit = this->properties[PROP_CTRL_HALT]->boolean_value ? 1 : 0;
    uint16_t new_set_point_bit = new_set_point ? 1 : 0;

    return build_ctrl_base_word(1, 1, 1, ena_op_bit, halt_bit) | build_ctrl_pos_prof_word(new_set_point_bit, 1, 0);
}

void CanOpenMotor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    uint8_t function, dst_node_id;
    unwrap_cob_id(id, function, dst_node_id);

    assert(dst_node_id == node_id);

    switch (function) {
    case COB_HEARTBEAT:
        handle_heartbeat(data);
        break;

    case COB_SDO_SERVER2CLIENT:
        handle_sdo_reply(data);
        break;

    case COB_TPDO1:
        handle_tpdo1(data);
        break;

    case COB_TPDO2:
        handle_tpdo2(data);
        break;
    }
}

bool CanOpenMotor::is_running() {
    return this->properties[PROP_VELOCITY]->integer_value != 0;
}

void CanOpenMotor::stop() {
    this->properties[PROP_CTRL_ENA_OP]->boolean_value = false;
    this->send_control_word(build_ctrl_word(false));
    this->properties[PROP_CTRL_HALT]->boolean_value = true;
    this->send_control_word(build_ctrl_word(false));
}

double CanOpenMotor::position() {
    return static_cast<double>(this->properties[PROP_POSITION]->integer_value);
}

void CanOpenMotor::position(const double position, const double speed, const uint32_t acceleration) {
    this->enter_position_mode(static_cast<int32_t>(speed));
    this->send_target_position(static_cast<int32_t>(position) + this->properties[PROP_OFFSET]->integer_value);
    send_control_word(build_ctrl_word(true));
    send_control_word(build_ctrl_word(false));
}

double CanOpenMotor::speed() {
    return static_cast<double>(this->properties[PROP_VELOCITY]->integer_value);
}

void CanOpenMotor::speed(const double speed, const uint32_t acceleration) {
    this->enter_velocity_mode(speed);
}