#include "bus_telemetry.h"
#include "../compilation/expressions.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "module_helpers.h"
#include <cstring>
#include <stdexcept>

static constexpr unsigned long MISSING_WARNING_MS = 5000;

static Module_ptr create_bus_telemetry(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler) {
    Module::expect(arguments, 2, identifier, integer);
    const SerialBus_ptr bus = get_module_argument<SerialBus>(arguments[0]);
    const uint8_t peer_id = SerialBus::to_peer_id(arguments[1]);
    if (peer_id == bus->node_id) {
        throw std::runtime_error("peer ID must differ from the node ID of the bus");
    }
    return std::make_shared<BusTelemetry>(name, bus, peer_id);
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
    // the layout lines with this name may have passed already, e.g. for a declaration at the console
    this->bus->forget_telemetry_layout(this->peer_id);
}

int BusTelemetry::find_mirror(const std::string_view name) const {
    for (size_t i = 0; i < this->mirrors.size(); ++i) {
        if (*this->mirrors[i].name == name) {
            return i;
        }
    }
    return -1;
}

bool BusTelemetry::is_mapped(const size_t mirror) const {
    for (const auto &[frame_id, frame] : this->frames) {
        for (const Field &field : frame.fields) {
            if (field.mirror == mirror) {
                return true;
            }
        }
    }
    return false;
}

void BusTelemetry::restart_missing_check() {
    this->frames_arriving = false;
    this->missing_checked = false;
}

void BusTelemetry::handle_layout_line(const telemetry::LayoutLine &line, const bool redefined) {
    const auto frame = this->frames.find(line.frame_id);
    if (frame != this->frames.end()) {
        if (redefined) {
            this->frames.erase(frame); // the peer gave the frame ID to other fields, so no mirror may take their values
        } else {
            // the line describes the field at its index, which only changes if a reused frame ID got the same layout ID
            std::vector<Field> &fields = frame->second.fields;
            for (auto field = fields.begin(); field != fields.end();) {
                field = field->index == line.index ? fields.erase(field) : std::next(field);
            }
            frame->second.positioned = false; // the positions follow from the types of all fields
        }
    }
    const int mirror = this->find_mirror(line.name);
    if (mirror < 0) {
        return;
    }
    if (to_type(line.type) != this->mirrors[mirror].variable->type) {
        // the field keeps out of the property, the rest of the frame is still mapped
        if (!this->mirrors[mirror].mismatch) {
            echo("warning: %s: peer %u sends \"%s\" as %s, not %s", this->name.c_str(), this->peer_id,
                 this->mirrors[mirror].name->c_str(), describe(to_type(line.type)), describe(this->mirrors[mirror].variable->type));
            this->mirrors[mirror].mismatch = true;
        }
        return;
    }
    FrameMap &map = this->frames[line.frame_id];
    map.fields.push_back({static_cast<uint16_t>(line.index), static_cast<uint16_t>(mirror), 0});
    map.positioned = false;
}

void BusTelemetry::handle_frame(const uint8_t *body, const std::vector<char> *types) {
    if (!this->frames_arriving) {
        this->frames_arriving = true;
        this->frames_since_millis = millis();
    } else if (!this->missing_checked && millis_since(this->frames_since_millis) >= MISSING_WARNING_MS) {
        this->missing_checked = true;
        for (size_t i = 0; i < this->mirrors.size(); ++i) {
            Mirror &mirror = this->mirrors[i];
            if (!mirror.warned && !mirror.mismatch && !this->is_mapped(i)) {
                mirror.warned = true;
                echo("warning: %s: no frame of peer %u carries \"%s\"", this->name.c_str(), this->peer_id, mirror.name->c_str());
            }
        }
    }
    const auto it = this->frames.find(body[0]);
    if (!types || it == this->frames.end()) {
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
