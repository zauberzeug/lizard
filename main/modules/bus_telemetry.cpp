#include "bus_telemetry.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "module_helpers.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

static constexpr size_t MAX_ORDER_LENGTH = 250; // a bus payload holds 255 characters
static constexpr size_t MAX_ORDER_REFERENCES = 40;
static constexpr unsigned long RENEW_MIN_MS = 5000; // no frame for this long (or 10 intervals): order everything again

static Module_ptr create_bus_telemetry(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler) {
    if (arguments.size() != 2 && arguments.size() != 3) {
        throw std::runtime_error("expecting 2 or 3 arguments (bus, node id[, interval ms])");
    }
    Module::expect(arguments, -1, identifier, integer, integer);
    const SerialBus_ptr bus = get_module_argument<SerialBus>(arguments[0]);
    const int64_t peer_id = arguments[1]->evaluate_integer();
    if (peer_id <= 0 || peer_id >= 255) {
        throw std::runtime_error("node ID must be between 1 and 254");
    }
    // 100 ms unless asked for otherwise: every step of four peers would take most of a 460800 bus
    const int64_t interval = arguments.size() > 2 ? arguments[2]->evaluate_integer() : 100;
    if (interval < 0) {
        throw std::runtime_error("interval must not be negative");
    }
    return std::make_shared<BusTelemetry>(name, bus, static_cast<uint8_t>(peer_id), static_cast<unsigned long>(interval));
}
REGISTER_MODULE(BusTelemetry, &create_bus_telemetry)

const std::map<std::string, Variable_ptr> BusTelemetry::get_defaults() {
    return {
        {"age", std::make_shared<IntegerVariable>(0)},    // milliseconds since the last frame
        {"millis", std::make_shared<IntegerVariable>(0)}, // the peer's core.millis in the last frame
        {"frames", std::make_shared<IntegerVariable>(0)},
    };
}

BusTelemetry::BusTelemetry(const std::string name, const SerialBus_ptr bus, const uint8_t peer_id, const unsigned long interval)
    : Module(name), bus(bus), peer_id(peer_id), interval(interval) {
    this->properties = BusTelemetry::get_defaults();
    this->age = this->properties.at("age");
    this->peer_millis = this->properties.at("millis");
    this->frames = this->properties.at("frames");
    this->bus->add_telemetry_listener(this);
}

BusTelemetry::~BusTelemetry() {
    this->bus->remove_telemetry_listener(this);
}

void BusTelemetry::step() {
    if (!this->orders_sent && !this->declared.empty()) {
        this->bus->request_telemetry_orders(this); // after the startup, i.e. once all declarations are known
    } else if (this->orders_sent && !this->bus->reads_other_format(this->peer_id)) {
        // a lost order or layout line, or a peer that forgot its orders: order again, at most every renew period
        const unsigned long renew_ms = std::max<unsigned long>(RENEW_MIN_MS, 10 * this->interval);
        const unsigned long quiet = this->frame_seen ? millis_since(this->last_frame_millis) : millis_since(this->orders_millis);
        if (quiet > renew_ms && millis_since(this->orders_millis) > renew_ms) {
            // nothing arrives: the same order again; a peer that still has the frames only announces their layout
            // again, one that lost them (e.g. rebooted with a lost Ready.) defines them anew, and a line lost now shows
            // as a name without layout; the mapping stays for frames that come back unchanged
            this->announced.clear();
            this->send_orders();
        } else if (millis_since(this->orders_millis) > renew_ms && millis_since(this->checked_millis) > renew_ms) {
            this->checked_millis = millis();
            this->resend_missing(renew_ms);
        }
    }
    this->age->set_integer_value(this->frame_seen ? millis_since(this->last_frame_millis) : millis());
    Module::step();
}

void BusTelemetry::declare_property(const std::string &property_name, const Variable_ptr &variable) {
    if (property_name == "age" || property_name == "millis" || property_name == "frames") {
        throw std::runtime_error("\"" + property_name + "\" is a property of " + this->name + " itself");
    }
    if (variable->type != boolean && variable->type != integer && variable->type != number) {
        throw std::runtime_error("mirrored properties must be bool, int or float");
    }
    const BusTelemetry *const other = this->bus->declaring_listener(this->peer_id, property_name);
    if (other && other != this) {
        throw std::runtime_error("\"" + property_name + "\" is already declared by " + other->name);
    }
    if (this->declares(property_name)) {
        const Variable_ptr existing = this->properties.at(property_name);
        if (existing->type != variable->type) {
            throw std::runtime_error("\"" + property_name + "\" is already declared with another type");
        }
        return;
    }
    const auto inserted = this->properties.emplace(property_name, variable).first;
    this->declared.push_back(&inserted->first);
    if (this->orders_sent) {
        this->send_order({&inserted->first}); // declared at the console after the orders went out
    }
}

void BusTelemetry::write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) {
    throw std::runtime_error("\"" + this->name + "." + property_name + "\" mirrors node " + std::to_string(this->peer_id) +
                             " and is read-only");
}

bool BusTelemetry::declares(const std::string &name) const {
    return std::any_of(this->declared.begin(), this->declared.end(), [&](const std::string *key) { return *key == name; });
}

void BusTelemetry::send_order(const std::vector<const std::string *> &names) {
    // one line per group of names, split at the bus payload or 40 references; the groups are kept, so that a lost
    // line goes out again unchanged, which the peer knows and answers with its layout instead of a new frame
    const size_t suffix = this->interval ? std::to_string(this->interval).size() + 3 : 1;
    std::vector<const std::string *> group;
    size_t length = 0;
    for (size_t i = 0; i <= names.size(); ++i) {
        const bool flush = i == names.size() || group.size() == MAX_ORDER_REFERENCES ||
                           (!group.empty() && length + 2 + names[i]->size() + suffix > MAX_ORDER_LENGTH);
        if (flush && !group.empty()) {
            this->bus->send_to(this->peer_id, this->order_line(group));
            this->order_groups.push_back(group);
            group.clear();
            length = 0;
        }
        if (i < names.size()) {
            length += (group.empty() ? std::strlen("core.telemetry(") : 2) + names[i]->size();
            group.push_back(names[i]);
        }
    }
}

std::string BusTelemetry::order_line(const std::vector<const std::string *> &group) const {
    std::string line = "core.telemetry(";
    for (size_t i = 0; i < group.size(); ++i) {
        line += (i ? ", " : "") + *group[i];
    }
    return line + (this->interval ? ", " + std::to_string(this->interval) + ")" : ")");
}

const std::string *BusTelemetry::declared_name(const Variable *variable) const {
    for (const std::string *name : this->declared) {
        if (this->properties.at(*name).get() == variable) {
            return name;
        }
    }
    return nullptr;
}

void BusTelemetry::resend_missing(const unsigned long renew_ms) {
    // lines whose names no layout line brought (lost or rejected) or whose frame fell silent (lost by the peer, e.g.
    // after a reboot whose Ready. got lost, while other frames keep age fresh) go out again unchanged
    std::vector<const std::string *> missing;
    for (const std::string *name : this->declared) {
        if (std::find(this->announced.begin(), this->announced.end(), name) == this->announced.end()) {
            missing.push_back(name);
        }
    }
    for (auto const &[frame_id, variables] : this->mapped) {
        const auto last = this->frame_millis.find(frame_id);
        if (last != this->frame_millis.end() && millis_since(last->second) <= renew_ms) {
            continue;
        }
        for (const Variable *variable : variables) {
            const std::string *name = variable ? this->declared_name(variable) : nullptr;
            if (name) {
                missing.push_back(name);
            }
        }
    }
    bool sent = false;
    for (auto const &group : this->order_groups) {
        if (std::any_of(group.begin(), group.end(),
                        [&](const std::string *name) { return std::find(missing.begin(), missing.end(), name) != missing.end(); })) {
            this->bus->send_to(this->peer_id, this->order_line(group));
            sent = true;
        }
    }
    if (sent) {
        this->orders_millis = millis();
    }
}

void BusTelemetry::send_orders() {
    this->order_groups.clear();
    this->send_order(this->declared);
    this->orders_sent = true;
    this->orders_millis = millis();
}

void BusTelemetry::reset_layout() {
    this->mapped.clear();
    this->slots.clear();
    this->announced.clear();
    this->frame_millis.clear();
}

void BusTelemetry::forget_frame(const uint8_t frame_id) {
    // its mirrors count as not announced any more, so that the next check orders them again
    for (Variable *const variable : this->mapped[frame_id]) {
        for (auto it = this->announced.begin(); variable && it != this->announced.end();) {
            it = this->properties.at(**it).get() == variable ? this->announced.erase(it) : std::next(it);
        }
    }
    this->mapped.erase(frame_id);
    this->slots.erase(frame_id);
    this->frame_millis.erase(frame_id);
}

void BusTelemetry::handle_layout_line(const telemetry::LayoutLine &line, const telemetry::Layout &layout) {
    const auto key = std::find_if(this->declared.begin(), this->declared.end(), [&](const std::string *name) { return *name == line.name; });
    if (key == this->declared.end()) {
        const auto frame = this->mapped.find(line.frame_id);
        if (frame != this->mapped.end() && line.index < frame->second.size() && frame->second[line.index]) {
            // another field where one of our mirrors was: the peer gave the frame id to other fields (e.g. after a
            // reboot whose Ready. got lost), so nothing of our old mapping of it is safe, also if a line of it got lost
            this->forget_frame(line.frame_id);
            return;
        }
    }
    Variable *variable = nullptr;
    if (key != this->declared.end()) {
        if (std::find(this->announced.begin(), this->announced.end(), *key) == this->announced.end()) {
            this->announced.push_back(*key);
        }
        variable = this->properties.at(line.name).get();
        if (!telemetry::type_matches(*variable, line.type)) {
            // the mirror keeps its value, the rest of the frame is still mapped
            echo("warning: %s: node %u sends \"%s\" as type %c", this->name.c_str(), this->peer_id, line.name.c_str(), line.type);
            variable = nullptr;
        }
    } else if (!this->mapped.count(line.frame_id)) {
        return; // a frame without any of our mirrors
    }
    // every line of a frame we mirror sets its field anew: when the peer gave the frame id to other fields, the old
    // mirrors must not stay at their indices
    std::vector<Variable *> &variables = this->mapped[line.frame_id];
    if (line.count > 0 && variables.size() > line.count) {
        variables.resize(line.count);
    }
    if (variables.size() <= line.index) {
        variables.resize(line.index + 1, nullptr);
    }
    variables[line.index] = variable;
    if (std::none_of(variables.begin(), variables.end(), [](const Variable *v) { return v != nullptr; })) {
        this->mapped.erase(line.frame_id);
        this->slots.erase(line.frame_id);
        this->frame_millis.erase(line.frame_id);
        return;
    }
    this->frame_millis[line.frame_id] = millis(); // announced: it has a renew period to deliver
    const auto types = layout.frames.find(line.frame_id);
    if (types != layout.frames.end()) {
        this->slots[line.frame_id] = telemetry::map_frame(types->second, variables);
    }
}

bool BusTelemetry::handle_frame(const uint8_t frame_id, const uint8_t seq, const uint32_t peer_millis, const uint8_t *payload) {
    const auto it = this->slots.find(frame_id);
    if (it == this->slots.end() || it->second.empty()) {
        return false; // not ours, or its layout is not complete yet
    }
    telemetry::apply(it->second, payload);
    this->frame_millis[frame_id] = millis();
    this->frame_seen = true;
    this->last_frame_millis = millis();
    this->peer_millis->set_integer_value(peer_millis);
    this->frames->set_integer_value(this->frames->integer_value() + 1);
    return true;
}
