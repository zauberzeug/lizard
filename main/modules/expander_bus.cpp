#include "expander_bus.h"
#include "../compilation/expressions.h"
#include "../utils/serial-replicator.h"
#include "../utils/string_utils.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "module_helpers.h"
#include "soc/uart_channel.h"
#include <cstring>
#include <stdexcept>

static constexpr uint8_t NODE_ID = 1;
static constexpr uint8_t PEER_ID = 2;
static constexpr size_t OUTGOING_QUEUE_LENGTH = 8; // commands go one at a time; the expander is never updated over the bus

static Module_ptr create_expander_bus(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler) {
    Module::expect(arguments, 3, identifier, integer, integer);
    const ConstSerial_ptr serial = get_module_argument<const Serial>(arguments[0]);
    if (serial->on_console) {
        throw std::runtime_error("expander bus \"" + name + "\" cannot use the console pins"); // a serial bus would take them over
    }
    const gpio_num_t boot_pin = static_cast<gpio_num_t>(arguments[1]->evaluate_integer());
    const gpio_num_t enable_pin = static_cast<gpio_num_t>(arguments[2]->evaluate_integer());
    return std::make_shared<ExpanderBus>(name, serial, boot_pin, enable_pin);
}
REGISTER_MODULE(ExpanderBus, &create_expander_bus)

ExpanderBus::ExpanderBus(const std::string &name, const ConstSerial_ptr serial, const gpio_num_t boot_pin, const gpio_num_t enable_pin)
    : SerialBus(name, serial, NODE_ID, {boot_pin, enable_pin}, OUTGOING_QUEUE_LENGTH), boot_pin(boot_pin), enable_pin(enable_pin),
      telemetry(name, SerialBus_ptr(SerialBus_ptr(), this), PEER_ID) { // the mirrors live as long as this bus, so they do not own it
    this->drive_pins_high();
    this->expect_peer_boot(); // the reset of this node may have restarted the expander as well
    this->set_peers(&PEER_ID, 1);
}

void ExpanderBus::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "send") {
        // p0.send(fmt[, args...]) is bus.send(2, fmt[, args...]), which configure.py --serial-bus 2 sends as well
        std::vector<ConstExpression_ptr> bus_arguments = arguments;
        if (arguments.empty() || (arguments[0]->type & integer) == 0) {
            bus_arguments.insert(bus_arguments.begin(), std::make_shared<IntegerExpression>(PEER_ID));
        } else if (arguments[0]->evaluate_integer() != PEER_ID) {
            throw std::runtime_error("the expander of expander bus \"" + this->name + "\" is node " + std::to_string(PEER_ID));
        }
        SerialBus::call(method_name, bus_arguments);
        if (bus_arguments.size() == 2 && bus_arguments[1]->evaluate_string() == "core.restart()") {
            this->expect_peer_boot(); // the last line of configure.py
        }
    } else if (method_name == "flash") {
        Module::expect(arguments, 0);
        this->flash();
    } else if (method_name == "restart") {
        Module::expect(arguments, 0);
        this->restart();
    } else if (method_name == "make_coordinator") {
        throw std::runtime_error("expander bus \"" + this->name + "\" polls only its expander");
    } else {
        SerialBus::call(method_name, arguments);
    }
}

void ExpanderBus::write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) {
    if (this->telemetry.declares(property_name)) {
        this->telemetry.write_property(property_name, expression, from_expander); // throws: a mirror is read-only
    }
    SerialBus::write_property(property_name, expression, from_expander);
}

void ExpanderBus::declare_property(const std::string &property_name, const Variable_ptr &variable) {
    if (this->properties.count(property_name) && !this->telemetry.declares(property_name)) {
        throw std::runtime_error("\"" + property_name + "\" is a property of expander bus \"" + this->name + "\"");
    }
    this->telemetry.declare_property(property_name, variable);
    this->properties[property_name] = this->telemetry.get_property(property_name); // the mirror, also if it existed
}

void ExpanderBus::drive_pins_high() const {
    // high before they turn into outputs, so that the expander does not restart when this node boots
    for (const gpio_num_t pin : {this->boot_pin, this->enable_pin}) {
        gpio_reset_pin(pin);
        gpio_set_level(pin, 1);
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    }
}

void ExpanderBus::restart() {
    gpio_set_level(this->enable_pin, 0);
    delay(100);
    gpio_set_level(this->enable_pin, 1);
    this->expect_peer_boot();
}

void ExpanderBus::flash() {
    this->serial->require_sole_user(this->name);
    this->pause_communication();
    struct Resume {
        ExpanderBus *const bus;
        ~Resume() { bus->resume_communication(); }
    } resume{this};
    this->serial->deinstall();
    // the replicator raises the rate eightfold to 921600 baud, as from an expander at the usual 115200 baud
    const bool success = ZZ::Replicator::flash_replica(this->serial->uart_num, this->enable_pin, this->boot_pin,
                                                       this->serial->rx_pin, this->serial->tx_pin, CONSOLE_DEFAULT_BAUD_RATE);
    delay(100);
    this->serial->reinitialize_after_flash();
    this->drive_pins_high();
    if (!success) {
        throw std::runtime_error("could not flash the expander of expander bus \"" + this->name + "\"");
    }
    this->configure_flashed_expander();
}

void ExpanderBus::configure_flashed_expander() {
    // The copy boots with an empty NVS, i.e. with its console at the default baud rate and without a startup script.
    // Through that console it gets a startup script with a serial bus on the console, which takes the console over
    // after a restart and lands in the expander's bus backup.
    const uart_port_t uart_num = this->serial->uart_num;
    uart_set_baudrate(uart_num, CONSOLE_DEFAULT_BAUD_RATE);
    this->restart();
    this->serial->flush();
    bool ready = false;
    const unsigned long start = millis();
    while (!ready && millis_since(start) < PEER_BOOT_MS) {
        char line[128]; // longer boot log lines are discarded by read_line()
        while (!ready && this->serial->has_buffered_lines()) {
            const int len = this->serial->read_line(line, sizeof(line));
            bool checksum_ok = false;
            ready = len > 0 && (check(line, len, &checksum_ok), checksum_ok) && std::strcmp(line, "Ready.") == 0;
        }
        delay(10);
    }
    if (ready) {
        char command[64];
        const int rx_pin = UART_NUM_0_RXD_DIRECT_GPIO_NUM; // a copy of this firmware runs on the same chip
        const int tx_pin = UART_NUM_0_TXD_DIRECT_GPIO_NUM;
        this->serial->write_checked_line("!-");
        this->serial->write_checked_line(command, csprintf(command, sizeof(command), "!+serial = Serial(%d, %d, %ld, 0)",
                                                           rx_pin, tx_pin, this->serial->baud_rate));
        this->serial->write_checked_line(command, csprintf(command, sizeof(command), "!+bus = SerialBus(serial, %u)", PEER_ID));
        this->serial->write_checked_line("!.");
        this->serial->write_checked_line("core.restart()");
        uart_wait_tx_done(uart_num, pdMS_TO_TICKS(500));
        delay(100); // before polls at another baud rate could garble the lines
        this->expect_peer_boot();
    }
    uart_set_baudrate(uart_num, this->serial->baud_rate);
    this->serial->flush();
    if (!ready) {
        throw std::runtime_error("the expander flashed by expander bus \"" + this->name + "\" did not report \"Ready.\"");
    }
    echo("expander bus %s flashed its expander, which restarts as bus node %u", this->name.c_str(), PEER_ID);
}
