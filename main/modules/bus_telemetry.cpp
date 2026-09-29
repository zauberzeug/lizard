#include "bus_telemetry.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "module_helpers.h"
#include <algorithm>
#include <stdexcept>

static constexpr size_t MAX_ORDER_LENGTH = 250; // a bus payload holds 255 characters
static constexpr size_t MAX_ORDER_REFERENCES = 40;

static Module_ptr create_bus_telemetry(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler) {
    if (arguments.size() != 2 && arguments.size() != 3) {
        throw std::runtime_error("expecting 2 or 3 arguments (bus, node id[, interval ms])");
    }
    Module::expect(arguments, -1, identifier, integer, integer);
    const SerialBus_ptr bus = get_module_argument<SerialBus>(arguments[0]);
    const int64_t peer_id = arguments[1]->evaluate_integer();
    if (peer_id <= 0 || peer_id >= 255) {
        throw std::runtime_error("node ID must be between 0 and 255");
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
    const std::string suffix = this->interval ? ", " + std::to_string(this->interval) + ")" : ")";
    std::string line;
    size_t references = 0;
    for (size_t i = 0; i <= names.size(); ++i) {
        const bool flush = i == names.size() || references == MAX_ORDER_REFERENCES ||
                           (references && line.size() + 2 + names[i]->size() + suffix.size() > MAX_ORDER_LENGTH);
        if (flush && references) {
            this->bus->send_to(this->peer_id, line + suffix);
            line.clear();
            references = 0;
        }
        if (i < names.size()) {
            line += (references ? ", " : "core.telemetry(") + *names[i];
            ++references;
        }
    }
}

void BusTelemetry::send_orders() {
    this->send_order(this->declared);
    this->orders_sent = true;
}

void BusTelemetry::reset_layout() {
    this->mapped.clear();
    this->slots.clear();
}

void BusTelemetry::handle_layout_line(const telemetry::LayoutLine &line, const telemetry::Layout &layout) {
    if (!this->declares(line.name)) {
        return;
    }
    Variable *const variable = this->properties.at(line.name).get();
    if (!telemetry::type_matches(*variable, line.type)) {
        echo("warning: %s: node %u sends \"%s\" as type %c", this->name.c_str(), this->peer_id, line.name.c_str(), line.type);
        return;
    }
    std::vector<Variable *> &variables = this->mapped[line.frame_id];
    if (variables.size() <= line.index) {
        variables.resize(line.index + 1, nullptr);
    }
    variables[line.index] = variable;
    const auto types = layout.frames.find(line.frame_id);
    if (types != layout.frames.end()) {
        this->slots[line.frame_id] = telemetry::map_frame(types->second, variables);
    }
}

bool BusTelemetry::handle_frame(const uint8_t frame_id, const uint8_t seq, const uint32_t peer_millis, const uint8_t *payload) {
    const auto it = this->slots.find(frame_id);
    if (it == this->slots.end()) {
        return false;
    }
    telemetry::apply(it->second, payload);
    this->frame_seen = true;
    this->last_frame_millis = millis();
    this->peer_millis->set_integer_value(peer_millis);
    this->frames->set_integer_value(this->frames->integer_value() + 1);
    return true;
}
