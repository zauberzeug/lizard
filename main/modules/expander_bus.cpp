#include "expander_bus.h"
#include "../utils/format.h"
#include "../utils/otb.h"
#include "module_helpers.h"
#include <stdexcept>

// the expander's messages come one at a time, its telemetry in a few frames; it is never updated over the bus
static constexpr size_t QUEUE_LENGTH = 8;
static constexpr size_t TELEMETRY_QUEUE_RESERVE = 2;

static Module_ptr create_expander_bus(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler) {
    Module::expect(arguments, 3, identifier, integer, integer);
    const ConstSerial_ptr serial = get_module_argument<const Serial>(arguments[0]);
    const gpio_num_t boot_pin = static_cast<gpio_num_t>(arguments[1]->evaluate_integer());
    const gpio_num_t enable_pin = static_cast<gpio_num_t>(arguments[2]->evaluate_integer());
    return std::make_shared<ExpanderBus>(name, serial, boot_pin, enable_pin);
}
REGISTER_MODULE(ExpanderBus, &create_expander_bus)

ExpanderBus::ExpanderBus(const std::string &name, const ConstSerial_ptr serial, const gpio_num_t boot_pin, const gpio_num_t enable_pin)
    : SerialBus(name, serial, NODE_ID, boot_pin, enable_pin, QUEUE_LENGTH, TELEMETRY_QUEUE_RESERVE) {
    // the mirrors live as long as this bus, so they get a pointer that does not own it
    this->telemetry = std::make_unique<BusTelemetry>(name, SerialBus_ptr(SerialBus_ptr(), this), PEER_ID);
    this->expect_peer_boot(); // the reset of this node may have restarted the expander as well
    const uint8_t peer_id = PEER_ID;
    this->set_peers(&peer_id, 1);
}

ExpanderBus::~ExpanderBus() {
    this->telemetry.reset(); // before the bus it listens to
}

void ExpanderBus::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "send") {
        // p0.send(fmt[, args...]) to the expander, printf-style like bus.send; p0.send(2, fmt[, args...]) as from configure.py
        const size_t format_index = !arguments.empty() && (arguments[0]->type & integer) ? 1 : 0;
        if (format_index == 1 && arguments[0]->evaluate_integer() != PEER_ID) {
            throw std::runtime_error("the expander of expander bus \"" + this->name + "\" is node " + std::to_string(PEER_ID));
        }
        if (arguments.size() <= format_index || (arguments[format_index]->type & string) == 0) {
            throw std::runtime_error("send expects a format string and its arguments");
        }
        const std::string payload = format_args(arguments[format_index]->evaluate_string(), arguments, format_index + 1);
        if (payload.rfind(otb::OTB_MSG_PREFIX, 0) == 0) {
            throw std::runtime_error("expander bus \"" + this->name + "\" does not update its expander over the bus, use flash()");
        }
        this->enqueue_outgoing_message(PEER_ID, payload.c_str(), payload.size());
    } else if (method_name == "flash") {
        Module::expect(arguments, 0);
        this->flash_peer(PEER_ID);
    } else if (method_name == "restart") {
        Module::expect(arguments, 0);
        this->restart_peer();
    } else if (method_name == "make_coordinator") {
        throw std::runtime_error("expander bus \"" + this->name + "\" polls only its expander");
    } else {
        SerialBus::call(method_name, arguments);
    }
}

void ExpanderBus::write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) {
    if (this->telemetry->declares(property_name)) {
        this->telemetry->write_property(property_name, expression, from_expander); // throws: a mirror is read-only
    }
    SerialBus::write_property(property_name, expression, from_expander);
}

void ExpanderBus::declare_property(const std::string &property_name, const Variable_ptr &variable) {
    if (this->properties.count(property_name) && !this->telemetry->declares(property_name)) {
        throw std::runtime_error("\"" + property_name + "\" is a property of expander bus \"" + this->name + "\"");
    }
    this->telemetry->declare_property(property_name, variable);
    this->properties[property_name] = this->telemetry->get_property(property_name); // the mirror, also if it existed
}
