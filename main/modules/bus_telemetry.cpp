#include "bus_telemetry.h"
#include "../compilation/expressions.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "module_helpers.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

static constexpr unsigned long DECLARATIONS_SETTLE_MS = 200;
static constexpr unsigned long LAYOUT_RETRY_MS = 2000;
static constexpr unsigned LAYOUT_RETRIES = 3;
static constexpr unsigned long MISSING_WARNING_MS = 5000;

static Module_ptr create_bus_telemetry(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler) {
    Module::expect(arguments, 2, identifier, integer);
    const SerialBus_ptr bus = get_module_argument<SerialBus>(arguments[0]);
    const long peer_id = arguments[1]->evaluate_integer();
    if (peer_id <= 0 || peer_id >= 255) {
        throw std::runtime_error("peer ID must be between 0 and 255");
    }
    if (peer_id == bus->node_id) {
        throw std::runtime_error("peer ID must differ from the node ID of the bus");
    }
    return std::make_shared<BusTelemetry>(name, bus, static_cast<uint8_t>(peer_id));
}
REGISTER_MODULE(BusTelemetry, &create_bus_telemetry)

static Type to_type(const char letter) {
    switch (letter) {
    case '?':
        return boolean;
    case 'i':
        return integer;
    default:
        return number;
    }
}

const std::map<std::string, Variable_ptr> BusTelemetry::get_defaults() {
    return {};
}

BusTelemetry::BusTelemetry(const std::string &name, const SerialBus_ptr bus, const uint8_t peer_id)
    : Module(name), bus(bus), peer_id(peer_id) {
    this->bus->add_telemetry_listener(this);
}

BusTelemetry::~BusTelemetry() {
    this->bus->remove_telemetry_listener(this);
}

void BusTelemetry::step() {
    if (this->layout_request_pending && millis_since(this->layout_request_millis) >= DECLARATIONS_SETTLE_MS &&
        this->bus->request_telemetry_layout(this->peer_id)) { // again while the queue is full
        this->layout_request_pending = false;
        this->layout_awaited = true;
        this->layout_request_millis = millis();
    }
    if (this->layout_awaited && millis_since(this->layout_request_millis) >= LAYOUT_RETRY_MS) {
        this->layout_awaited = false;
        if (this->layout_retries < LAYOUT_RETRIES) { // the request or its answer got lost
            ++this->layout_retries;
            this->request_layout();
        }
    }
    if (this->frames_arriving && !this->missing_checked && millis_since(this->frames_since_millis) >= MISSING_WARNING_MS) {
        this->missing_checked = true;
        for (Mirror &mirror : this->mirrors) {
            if (!mirror.received && !mirror.warned && !mirror.mismatch) {
                mirror.warned = true;
                echo("warning: %s: no frame of peer %u carries \"%s\"", this->name.c_str(), this->peer_id, mirror.name->c_str());
            }
        }
    }
    Module::step();
}

void BusTelemetry::write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) {
    if (this->declares(property_name)) {
        throw std::runtime_error("property \"" + this->name + "." + property_name + "\" mirrors peer " +
                                 std::to_string(this->peer_id) + " and is read-only");
    }
    Module::write_property(property_name, expression, from_expander);
}

void BusTelemetry::declare_property(const std::string &property_name, const Variable_ptr &variable) {
    if (variable->type != boolean && variable->type != integer && variable->type != number) {
        throw std::runtime_error("property \"" + this->name + "." + property_name + "\" must be a bool, int or float");
    }
    const auto it = this->properties.find(property_name);
    if (it != this->properties.end()) {
        if (it->second->type != variable->type) {
            throw std::runtime_error("property \"" + this->name + "." + property_name + "\" is " +
                                     describe(it->second->type) + ", not " + describe(variable->type));
        }
        if (!this->mirrors[this->find_mirror(property_name)].received) {
            it->second->assign(std::make_shared<VariableExpression>(variable));
        }
        return;
    }
    if (const BusTelemetry *const other = this->bus->find_telemetry_listener(this->peer_id, property_name)) {
        throw std::runtime_error("\"" + property_name + "\" of peer " + std::to_string(this->peer_id) +
                                 " is already declared by " + other->name);
    }
    const auto inserted = this->properties.emplace(property_name, variable).first;
    this->mirrors.push_back({&inserted->first, variable.get()});
    this->restart_missing_check();
    if (this->bus->has_telemetry_layout(this->peer_id)) {
        // the layout lines with this name may have passed already, so ask for them again once the declarations settle
        this->layout_retries = 0;
        this->request_layout();
    }
}

void BusTelemetry::request_layout() {
    this->layout_request_pending = true;
    this->layout_request_millis = millis();
}

int BusTelemetry::find_mirror(const std::string_view name) const {
    for (size_t i = 0; i < this->mirrors.size(); ++i) {
        if (*this->mirrors[i].name == name) {
            return i;
        }
    }
    return -1;
}

void BusTelemetry::restart_missing_check() {
    this->frames_arriving = false;
    this->missing_checked = false;
}

void BusTelemetry::handle_layout_line(const telemetry::LayoutLine &line) {
    this->layout_awaited = false;
    int mirror = this->find_mirror(line.name);
    if (mirror >= 0 && to_type(line.type) != this->mirrors[mirror].variable->type) {
        // the field keeps out of the property, the rest of the frame is still mapped
        if (!this->mirrors[mirror].mismatch) {
            echo("warning: %s: peer %u sends \"%s\" as %s, not %s", this->name.c_str(), this->peer_id,
                 this->mirrors[mirror].name->c_str(), describe(to_type(line.type)), describe(this->mirrors[mirror].variable->type));
            this->mirrors[mirror].mismatch = true;
        }
        mirror = -1;
    }
    auto frame = this->frames.find(line.frame_id);
    if (frame != this->frames.end()) {
        const bool other_layout = frame->second.layout_id != line.layout_id || frame->second.count != line.count;
        const bool other_field = std::any_of(frame->second.fields.begin(), frame->second.fields.end(), [&](const Field &field) {
            return field.index == line.index && field.mirror != mirror;
        });
        if (other_layout || other_field) {
            // the peer gave the frame ID to other fields, so no mirror of the old mapping may take their values
            this->frames.erase(frame);
            frame = this->frames.end();
            if (!other_layout) {
                // a new layout with the same layout ID: lines of it that came before this one were dropped with the mapping
                this->layout_retries = 0;
                this->request_layout();
            }
        } else {
            frame->second.positioned = false; // the positions follow from the types of all fields
        }
    }
    if (mirror < 0) {
        return;
    }
    if (frame == this->frames.end()) {
        frame = this->frames.emplace(line.frame_id, FrameMap{line.layout_id, line.count, {}}).first;
    }
    std::vector<Field> &fields = frame->second.fields;
    if (std::none_of(fields.begin(), fields.end(), [&](const Field &field) { return field.index == line.index; })) {
        fields.push_back({static_cast<uint16_t>(line.index), static_cast<uint16_t>(mirror), 0});
    }
}

void BusTelemetry::handle_frame(const uint8_t *body, const std::vector<char> *types) {
    if (!this->frames_arriving) {
        this->frames_arriving = true;
        this->frames_since_millis = millis();
    }
    const auto it = this->frames.find(body[0]);
    if (!types || it == this->frames.end() || it->second.layout_id != body[1] || it->second.count != types->size()) {
        return;
    }
    FrameMap &frame = it->second;
    if (!frame.positioned) {
        // the numbers come first, 4 bytes each, then the bools as bits
        size_t offset = 0;
        size_t bit = 0;
        for (size_t i = 0; i < types->size(); ++i) {
            const bool is_bool = (*types)[i] == '?';
            for (Field &field : frame.fields) {
                if (field.index == i) {
                    field.position = is_bool ? bit : offset;
                }
            }
            if (is_bool) {
                ++bit;
            } else {
                offset += 4;
            }
        }
        frame.bits_offset = offset;
        frame.positioned = true;
    }
    const uint8_t *const payload = body + telemetry::HEADER_SIZE;
    for (const Field &field : frame.fields) {
        Mirror &mirror = this->mirrors[field.mirror];
        switch (mirror.variable->type) {
        case boolean:
            mirror.variable->set_boolean_value(payload[frame.bits_offset + field.position / 8] >> field.position % 8 & 1);
            break;
        case integer: {
            int32_t value;
            memcpy(&value, &payload[field.position], 4);
            mirror.variable->set_integer_value(value);
            break;
        }
        default: {
            float value;
            memcpy(&value, &payload[field.position], 4);
            mirror.variable->set_number_value(value);
        }
        }
        mirror.received = true;
    }
}

void BusTelemetry::reset() {
    this->frames.clear();
    for (Mirror &mirror : this->mirrors) {
        mirror.received = false;
    }
    this->restart_missing_check();
}
