#include "core.h"
#include "../compilation/expressions.h"
#include "../global.h"
#include "../storage.h"
#include "../utils/bus_backup.h"
#include "../utils/interpreter_lock.h"
#include "../utils/scheduler.h"
#include "../utils/string_utils.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "driver/gpio.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "serial_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/gpio_hal.h"
#include "soc/io_mux_reg.h"
#include "soc/soc.h"
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <stdlib.h>

Core::Core(const std::string name) : Module(name) {
    this->properties["debug"] = std::make_shared<BooleanVariable>(false);
    this->properties["millis"] = std::make_shared<IntegerVariable>();
    this->properties["heap"] = std::make_shared<IntegerVariable>();
    this->properties["last_message_age"] = std::make_shared<IntegerVariable>();
    // experiment knobs and measurements
    this->properties["telemetry_compact"] = std::make_shared<BooleanVariable>(false); // new frames send numbers as float16
    this->properties["telemetry_info_rate"] = std::make_shared<IntegerVariable>(0);   // layout lines per step, 0: all at once
    this->properties["telemetry_us"] = std::make_shared<IntegerVariable>(0);
    this->properties["telemetry_us_max"] = std::make_shared<IntegerVariable>(0);
    this->properties["modules_us"] = std::make_shared<IntegerVariable>(0);
    this->properties["heap_largest"] = std::make_shared<IntegerVariable>(0); // largest free block, what a parse needs
    this->properties["heap_min"] = std::make_shared<IntegerVariable>(0);     // lowest free heap since boot
    this->properties["parse_bytes"] = std::make_shared<IntegerVariable>(0);  // heap the last parse tree took
    this->properties["parse_bytes_max"] = std::make_shared<IntegerVariable>(0);
    this->properties["rules_us"] = std::make_shared<IntegerVariable>(0);
}

void Core::record_parse(const int64_t bytes) {
    this->properties.at("parse_bytes")->set_integer_value(bytes);
    const Variable_ptr maximum = this->properties.at("parse_bytes_max");
    if (bytes > maximum->integer_value()) {
        maximum->set_integer_value(bytes);
    }
}

void Core::record_step_timing(const int64_t modules_us, const int64_t rules_us) {
    this->properties.at("modules_us")->set_integer_value(modules_us);
    this->properties.at("rules_us")->set_integer_value(rules_us);
}

void Core::step() {
    this->properties.at("millis")->set_integer_value(millis());
    this->properties.at("heap")->set_integer_value(xPortGetFreeHeapSize());
    this->properties.at("heap_largest")->set_integer_value(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    this->properties.at("heap_min")->set_integer_value(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    this->properties.at("last_message_age")->set_integer_value(millis_since(this->last_message_millis));
    Module::step();
}

void Core::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "restart") {
        Module::expect(arguments, 0);
        esp_restart();
    } else if (method_name == "version") {
        const esp_app_desc_t *app_desc = esp_app_get_description();
        echo("version: %s %s", app_desc->project_name, app_desc->version);
    } else if (method_name == "info") {
        Module::expect(arguments, 0);
        const esp_app_desc_t *app_desc = esp_app_get_description();
        echo("project name: %s", app_desc->project_name);
        echo("version: %s", app_desc->version);
        echo("compile time: %s, %s", app_desc->date, app_desc->time);
        echo("idf version: %s", app_desc->idf_ver);
    } else if (method_name == "print") {
        static char buffer[1024];
        int pos = 0;
        for (auto const &argument : arguments) {
            if (argument != arguments[0]) {
                pos += csprintf(&buffer[pos], sizeof(buffer) - pos, " ");
            }
            pos += argument->print_to_buffer(&buffer[pos], sizeof(buffer) - pos);
        }
        echo("%s", buffer);
    } else if (method_name == "output") {
        Module::expect(arguments, 1, string);
        this->output_list.clear();
        this->output_overflow_reported = false;
        std::string format = arguments[0]->evaluate_string();
        while (!format.empty()) {
            std::string element = cut_first_word(format);
            if (element.find('.') == std::string::npos) {
                // variable[:precision]
                std::string variable_name = cut_first_word(element, ':');
                const unsigned int precision = element.empty() ? 0 : atoi(element.c_str());
                this->output_list.push_back({nullptr, variable_name, precision});
            } else {
                // module.property[:precision]
                std::string module_name = cut_first_word(element, '.');
                const ConstModule_ptr module = Global::get_module(module_name);
                const std::string property_name = cut_first_word(element, ':');
                const unsigned int precision = element.empty() ? 0 : atoi(element.c_str());
                this->output_list.push_back({module, property_name, precision});
            }
        }
        this->output_on = true;
    } else if (method_name == "startup_checksum") {
        uint16_t checksum = 0;
        for (char const &c : Storage::startup) {
            checksum += static_cast<uint8_t>(c);
        }
        echo("checksum: %04x", checksum);
    } else if (method_name == "get_pin_status") {
        Module::expect(arguments, 1, integer);
        const int gpio_num = arguments[0]->evaluate_integer();
        if (gpio_num < 0 || gpio_num >= GPIO_NUM_MAX) {
            throw std::runtime_error("invalid pin");
        }

        bool pullup, pulldown, input_enabled, output_enabled, open_drain, sleep_sel_enabled;
        uint32_t drive_strength, func_sel, signal_output;
        static gpio_hal_context_t _gpio_hal = {.dev = GPIO_HAL_GET_HW(GPIO_PORT_0)};
        gpio_hal_get_io_config(&_gpio_hal, gpio_num, &pullup, &pulldown, &input_enabled, &output_enabled,
                               &open_drain, &drive_strength, &func_sel, &signal_output, &sleep_sel_enabled);

        const int gpio_level = gpio_get_level(static_cast<gpio_num_t>(gpio_num));

        echo("GPIO_Status[%d]| Level: %d| InputEn: %d| OutputEn: %d| OpenDrain: %d| Pullup: %d| Pulldown: %d| "
             "DriveStrength: %d| SleepSel: %d",
             gpio_num, gpio_level, input_enabled, output_enabled, open_drain, pullup, pulldown,
             drive_strength, sleep_sel_enabled);
    } else if (method_name == "set_pin_level") {
        Module::expect(arguments, 2, integer, integer);
        const int gpio_num = arguments[0]->evaluate_integer();
        if (gpio_num < 0 || gpio_num >= GPIO_NUM_MAX) {
            throw std::runtime_error("invalid pin");
        }
        const int value = arguments[1]->evaluate_integer();
        if (value < 0 || value > 1) {
            throw std::runtime_error("invalid value");
        }

        gpio_config_t io_conf;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = (1ULL << gpio_num);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&io_conf);

        const esp_err_t err = gpio_set_level(static_cast<gpio_num_t>(gpio_num), value);
        if (err != ESP_OK) {
            throw std::runtime_error("failed to set pin");
        }
        echo("GPIO_set[%d] set to %d", gpio_num, value);
    } else if (method_name == "get_pin_strapping") {
        Module::expect(arguments, 1, integer);
        const gpio_num_t gpio_num = static_cast<gpio_num_t>(arguments[0]->evaluate_integer());
        if (gpio_num < 0 || gpio_num >= GPIO_NUM_MAX) {
            throw std::runtime_error("invalid pin");
        }
        const uint32_t strapping_reg = REG_READ(GPIO_STRAP_REG);
#ifdef CONFIG_IDF_TARGET_ESP32
        // GPIO_STRAPPING is {10'b0, MTDI, GPIO0, GPIO2, GPIO4, MTDO, GPIO5}, see soc/gpio_reg.h
        switch (gpio_num) {
        case GPIO_NUM_0:
            echo("Strapping GPIO0: %d", (strapping_reg & BIT(4)) ? 1 : 0);
            break;
        case GPIO_NUM_2:
            echo("Strapping GPIO2: %d", (strapping_reg & BIT(3)) ? 1 : 0);
            break;
        case GPIO_NUM_4:
            echo("Strapping GPIO4: %d", (strapping_reg & BIT(2)) ? 1 : 0);
            break;
        case GPIO_NUM_5:
            echo("Strapping GPIO5: %d", (strapping_reg & BIT(0)) ? 1 : 0);
            break;
        case GPIO_NUM_12:
            echo("Strapping GPIO12 (MTDI): %d", (strapping_reg & BIT(5)) ? 1 : 0);
            break;
        case GPIO_NUM_15:
            echo("Strapping GPIO15 (MTDO): %d", (strapping_reg & BIT(1)) ? 1 : 0);
            break;
        default:
            echo("Not a strapping pin");
            break;
        }
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
        // The S3's soc/gpio_reg.h does not document the strapping layout. GPIO0 = bit 3 and GPIO46 = bit 2 follow
        // from soc/boot_mode.h (download boot requires both bits to be low); GPIO45 = bit 4 matches esptool's
        // GPIO_STRAP_VDDSPI_MASK. GPIO3 (JTAG source selection) is also a strapping pin, but no ESP-IDF or esptool
        // source documents its bit position, so we only report the raw register for it.
        switch (gpio_num) {
        case GPIO_NUM_0:
            echo("Strapping GPIO0: %d", (strapping_reg & BIT(3)) ? 1 : 0);
            break;
        case GPIO_NUM_45:
            echo("Strapping GPIO45: %d", (strapping_reg & BIT(4)) ? 1 : 0);
            break;
        case GPIO_NUM_46:
            echo("Strapping GPIO46: %d", (strapping_reg & BIT(2)) ? 1 : 0);
            break;
        case GPIO_NUM_3:
            echo("Strapping GPIO3: unknown bit position, register: 0x%04x", strapping_reg);
            break;
        default:
            echo("Not a strapping pin");
            break;
        }
#else
        // Other targets strap different pins into different bits; decode them here when a new target is added.
        echo("Strapping register: 0x%04x", strapping_reg);
#endif
    } else if (method_name == "forget_serial_bus") {
        Module::expect(arguments, 0);
        bus_backup::remove();
    } else if (method_name == "set_baudrate") {
        Module::expect(arguments, 1, integer);
        const int baudrate = arguments[0]->evaluate_integer();
        bool supported = false;
        for (const int rate : {115200, 230400, 460800, 921600}) {
            if (rate == baudrate) {
                supported = true;
                break;
            }
        }
        if (!supported) {
            throw std::runtime_error("unsupported baudrate (use 115200, 230400, 460800 or 921600)");
        }
        Storage::set_baudrate(baudrate);
        echo("baudrate set to %d; restart to apply", baudrate);
    } else if (method_name == "pause_broadcasts") {
        Module::expect(arguments, 0);
        Module::broadcast_paused = true;
        echo("broadcasts paused");
    } else if (method_name == "resume_broadcasts") {
        Module::expect(arguments, 0);
        Module::broadcast_paused = false;
        echo("broadcasts resumed");
    } else if (method_name == "clear_schedule") {
        Module::expect(arguments, 0);
        scheduler::clear();
    } else if (method_name == "keep_alive") {
        Module::expect(arguments, 0);
        this->keep_alive();
    } else if (method_name == "telemetry") {
        this->define_telemetry(arguments);
    } else if (method_name == "frame" || method_name == "frame_add" || method_name == "frame_clear") {
        // #290's frame definitions in existing startups: accepted and ignored, telemetry is ordered by the coordinator now
        static bool reported = false;
        if (!reported) {
            echo("warning: core.%s is ignored in this build, use core.telemetry", method_name.c_str());
            reported = true;
        }
    } else if (method_name == "telemetry_info") {
        Module::expect(arguments, 0);
        for (auto const &frame : this->telemetry_frames) {
            this->announce(frame);
        }
    } else if (method_name == "clear_telemetry") {
        Module::expect(arguments, 0);
        this->clear_telemetry();
    } else if (method_name == "telemetry_selftest") {
        // telemetry_selftest(floats): microseconds per frame for each stage, averaged over 200 rounds
        Module::expect(arguments, 1, integer);
        const int64_t count = std::max<int64_t>(1, std::min<int64_t>(45, arguments[0]->evaluate_integer()));
        std::vector<telemetry::Field> fields;
        std::vector<char> types;
        std::vector<Variable_ptr> targets;
        std::vector<Variable *> variables;
        static const std::string key = "x";
        for (int64_t i = 0; i < count; ++i) {
            fields.push_back({std::make_shared<NumberVariable>(1.5 * i - 7.25), nullptr, &key, 'f'});
            types.push_back('f');
            targets.push_back(std::make_shared<NumberVariable>());
            variables.push_back(targets.back().get());
        }
        const std::vector<telemetry::Slot> slots = telemetry::map_frame(types, variables);
        static uint8_t payload[telemetry::MAX_PAYLOAD];
        static uint8_t body[telemetry::MAX_BODY];
        static uint8_t decoded[telemetry::MAX_BODY + 3];
        static char line[telemetry::MAX_LINE];
        const int rounds = 200;
        size_t payload_length = 0, body_length = 0, line_length = 0, ok = 0;
        uint16_t crc = 0;
        int64_t t[10];
        t[0] = esp_timer_get_time();
        for (int r = 0; r < rounds; ++r) {
            payload_length = telemetry::pack(fields, payload, sizeof(payload));
        }
        t[1] = esp_timer_get_time();
        for (int r = 0; r < rounds; ++r) {
            body_length = telemetry::build_body(1, r, 1234, payload, payload_length, body);
        }
        t[2] = esp_timer_get_time();
        for (int r = 0; r < rounds; ++r) {
            line_length = telemetry::encode_line(body, body_length, line, sizeof(line));
        }
        t[3] = esp_timer_get_time();
        for (int r = 0; r < rounds; ++r) {
            ok += telemetry::decode_line(line, line_length, decoded, sizeof(decoded)) ? 1 : 0;
        }
        t[4] = esp_timer_get_time();
        for (int r = 0; r < rounds; ++r) {
            telemetry::apply(slots, &decoded[telemetry::HEADER_SIZE]);
        }
        t[5] = esp_timer_get_time();
        for (int r = 0; r < rounds; ++r) {
            crc ^= telemetry::crc16(body, body_length - telemetry::CRC_SIZE);
        }
        t[6] = esp_timer_get_time();
        for (int r = 0; r < rounds; ++r) {
            crc ^= telemetry::bitwise_crc16(body, body_length - telemetry::CRC_SIZE);
        }
        t[7] = esp_timer_get_time();
        for (int r = 0; r < rounds; ++r) {
            line_length = telemetry::mbedtls_encode_line(body, body_length, line, sizeof(line));
        }
        t[8] = esp_timer_get_time();
        for (int r = 0; r < rounds; ++r) {
            ok += telemetry::mbedtls_decode_line(line, line_length, decoded, sizeof(decoded)) ? 1 : 0;
        }
        t[9] = esp_timer_get_time();
        const auto us = [&](int i) { return (t[i + 1] - t[i]) / double(rounds); };
        echo("telemetry selftest: %d floats, body %u B, line %u B, ok %u/%d; us per frame: pack %.1f, body+crc %.1f, "
             "encode %.1f, decode+crc %.1f, apply %.1f; crc table %.1f, crc bitwise %.1f; mbedtls encode %.1f, "
             "mbedtls decode+bitwise crc %.1f (%04x)",
             static_cast<int>(count), static_cast<unsigned>(body_length), static_cast<unsigned>(line_length),
             static_cast<unsigned>(ok), 2 * rounds, us(0), us(1), us(2), us(3), us(4), us(5), us(6), us(7), us(8), crc);
        // #290's encodings on the same body, and a body of zeros (a robot standing still) for the byte counts
        static uint8_t cobs[telemetry::MAX_BODY + telemetry::MAX_BODY / 254 + 3];
        static char stuffed[2 * telemetry::MAX_BODY + 2];
        size_t cobs_length = 0, bus_length = 0, line_stuffed_length = 0, unstuffed = 0;
        const int64_t c0 = esp_timer_get_time();
        for (int r = 0; r < rounds; ++r) {
            cobs_length = telemetry::cobs_encode(body, body_length, cobs);
        }
        const int64_t c1 = esp_timer_get_time();
        for (int r = 0; r < rounds; ++r) {
            bus_length = telemetry::stuff(body, body_length, stuffed, sizeof(stuffed), false);
        }
        const int64_t c2 = esp_timer_get_time();
        for (int r = 0; r < rounds; ++r) {
            unstuffed = telemetry::unstuff(stuffed, bus_length, decoded, sizeof(decoded));
        }
        const int64_t c3 = esp_timer_get_time();
        line_stuffed_length = telemetry::stuff(body, body_length, stuffed, sizeof(stuffed), true);
        static uint8_t zeros[telemetry::MAX_BODY];
        memset(zeros, 0, sizeof(zeros));
        const size_t zero_body = std::min(body_length, sizeof(zeros));
        const size_t zero_cobs = telemetry::cobs_encode(zeros, zero_body, cobs);
        const size_t zero_bus = telemetry::stuff(zeros, zero_body, stuffed, sizeof(stuffed), false);
        const size_t zero_line = telemetry::stuff(zeros, zero_body, stuffed, sizeof(stuffed), true);
        const size_t zero_b64 = telemetry::encode_line(zeros, zero_body, line, sizeof(line));
        echo("telemetry selftest #290: body %u B -> COBS %u B (+2 delimiters), bus-stuffed %u B, line-stuffed %u B, "
             "base64 %u B; zeros: COBS %u, bus %u, line %u, base64 %u; us per frame: cobs encode %.1f, bus stuff %.1f, "
             "unstuff %.1f (%u)",
             static_cast<unsigned>(body_length), static_cast<unsigned>(cobs_length), static_cast<unsigned>(bus_length),
             static_cast<unsigned>(line_stuffed_length), static_cast<unsigned>(line_length), static_cast<unsigned>(zero_cobs),
             static_cast<unsigned>(zero_bus), static_cast<unsigned>(zero_line), static_cast<unsigned>(zero_b64),
             (c1 - c0) / double(rounds), (c2 - c1) / double(rounds), (c3 - c2) / double(rounds), static_cast<unsigned>(unstuffed));
    } else {
        Module::call(method_name, arguments);
    }
}

std::string Core::get_output() const {
    // Module::step sends this as "core <output>", so the "core " prefix comes off the payload budget (plus the terminator)
    static char output_buffer[CONSOLE_PAYLOAD_SIZE - 5 + 1];
    int pos = 0;
    try {
        for (auto const &element : this->output_list) {
            if (pos > 0) {
                pos += csprintf(&output_buffer[pos], sizeof(output_buffer) - pos, " ");
            }
            const Variable_ptr variable =
                element.module ? element.module->get_property(element.property_name) : Global::get_variable(element.property_name);
            switch (variable->type) {
            case boolean:
                pos += csprintf(&output_buffer[pos], sizeof(output_buffer) - pos, "%s", variable->boolean_value() ? "true" : "false");
                break;
            case integer:
                pos += csprintf(&output_buffer[pos], sizeof(output_buffer) - pos, "%lld", variable->integer_value());
                break;
            case number:
                pos += csprintf(&output_buffer[pos], sizeof(output_buffer) - pos, "%.*f", element.precision, variable->number_value());
                break;
            case string:
                pos += csprintf(&output_buffer[pos], sizeof(output_buffer) - pos, "\"%s\"", variable->string_value().c_str());
                break;
            default:
                throw std::runtime_error("invalid type");
            }
        }
        this->output_overflow_reported = false; // a line that fits again ends the episode, the next overflow is reported
    } catch (const BufferTooSmallError &) {
        // one error per episode instead of a warning every tick
        if (!this->output_overflow_reported) {
            echo("error: the core line with %d output fields exceeds %d bytes and is suppressed until it fits again",
                 static_cast<int>(this->output_list.size()), CONSOLE_LINE_SIZE);
            this->output_overflow_reported = true;
        }
        return "";
    }
    return std::string(output_buffer);
}

void Core::keep_alive() {
    this->last_message_millis = millis();
}

// where the name of a telemetry field lives: the property's key in its module, or the variable's key in Global
static bool field_source(const ConstExpression_ptr &argument, const Module *&module, const std::string *&key) {
    if (const auto property = std::dynamic_pointer_cast<const PropertyExpression>(argument)) {
        module = property->get_module().get();
        key = &property->get_module()->property_key(property->get_property_name());
        return true;
    }
    if (const auto variable = std::dynamic_pointer_cast<const VariableExpression>(argument)) {
        for (auto const &[name, candidate] : Global::variables) {
            if (candidate.get() == variable->get_variable().get()) {
                module = nullptr;
                key = &name;
                return true;
            }
        }
    }
    return false;
}

static std::string field_name(const telemetry::Field &field) {
    return field.module ? field.module->name + "." + *field.key : *field.key;
}

void Core::define_telemetry(const std::vector<ConstExpression_ptr> &arguments) {
    // telemetry(ref, ...[, interval_ms]): one call is one frame, references are resolved once, here
    std::vector<telemetry::Field> fields;
    unsigned long interval = 0;
    const bool compact = this->properties.at("telemetry_compact")->boolean_value();
    for (size_t i = 0; i < arguments.size(); ++i) {
        const ConstExpression_ptr &argument = arguments[i];
        ConstVariable_ptr variable;
        if (const auto property = std::dynamic_pointer_cast<const PropertyExpression>(argument)) {
            variable = property->get_module()->get_property(property->get_property_name());
        } else if (const auto reference = std::dynamic_pointer_cast<const VariableExpression>(argument)) {
            variable = reference->get_variable();
        } else if (i + 1 == arguments.size() && i > 0 && argument->type == integer) {
            const int64_t value = argument->evaluate_integer();
            if (value < 0 || value > 3600000) {
                throw std::runtime_error("telemetry interval must be between 0 and 3600000 ms");
            }
            interval = static_cast<unsigned long>(value);
            continue;
        } else {
            throw std::runtime_error("telemetry argument " + std::to_string(i) + " is neither a property nor a variable");
        }
        const Module *module = nullptr;
        const std::string *key = nullptr;
        if (!field_source(argument, module, key)) {
            throw std::runtime_error("telemetry argument " + std::to_string(i) + " has no name");
        }
        fields.push_back({variable, module, key, telemetry::type_for(variable, compact)});
    }
    if (fields.empty()) {
        throw std::runtime_error("telemetry needs at least one field");
    }
    if (telemetry::payload_size(fields) > telemetry::MAX_PAYLOAD) {
        throw std::runtime_error("too many fields for one telemetry frame (" + std::to_string(telemetry::payload_size(fields)) +
                                 " of " + std::to_string(telemetry::MAX_PAYLOAD) + " bytes)");
    }
    SerialBus *const bus = SerialBus::executing_bus;
    const uint8_t destination = bus ? SerialBus::executing_sender : 0;

    // the same fields and interval for the same requester: no new frame, but announce it again
    for (auto const &frame : this->telemetry_frames) {
        if (frame.bus == bus && frame.destination == destination && frame.interval == interval &&
            frame.fields.size() == fields.size() &&
            std::equal(frame.fields.begin(), frame.fields.end(), fields.begin(), [](const telemetry::Field &a, const telemetry::Field &b) {
                return a.variable == b.variable && a.type == b.type;
            })) {
            this->announce(frame);
            return;
        }
    }
    uint8_t id = 1;
    for (auto const &frame : this->telemetry_frames) {
        id = std::max<int>(id, frame.id + 1);
    }
    if (this->telemetry_frames.size() >= 255 || id == 0) {
        throw std::runtime_error("too many telemetry frames");
    }
    TelemetryFrame frame{};
    frame.id = id;
    frame.fields = std::move(fields);
    frame.interval = interval;
    frame.bus = bus;
    frame.destination = destination;
    frame.last_millis = millis() - interval; // first frame in this step
    this->telemetry_frames.push_back(std::move(frame));
    this->announce(this->telemetry_frames.back());
}

void Core::clear_telemetry() {
    // frames of the requester go: a bus node's orders over the bus, the console's own frames from the console
    SerialBus *const bus = SerialBus::executing_bus;
    const uint8_t destination = bus ? SerialBus::executing_sender : 0;
    for (auto const &frame : this->telemetry_frames) {
        if (frame.bus == bus && frame.destination == destination && bus) {
            bus->release_frame(frame.id);
        }
    }
    this->telemetry_frames.erase(std::remove_if(this->telemetry_frames.begin(), this->telemetry_frames.end(),
                                                [&](const TelemetryFrame &frame) {
                                                    return frame.bus == bus && frame.destination == destination;
                                                }),
                                 this->telemetry_frames.end());
    this->pending_layout.clear();
}

SerialBus *Core::polled_bus() const {
    for (auto const &[module_name, module] : Global::modules) {
        SerialBus *const bus = dynamic_cast<SerialBus *>(module.get());
        if (bus && bus->coordinator() != 0) {
            return bus;
        }
    }
    return nullptr;
}

bool Core::route(const TelemetryFrame &frame, SerialBus *polled, SerialBus *&bus, uint8_t &destination) const {
    if (frame.bus) {
        bus = frame.bus;
        destination = frame.destination;
        return true;
    }
    // the node's own frames go to the coordinator once one polls it, before that to the console
    if (polled) {
        bus = polled;
        destination = polled->coordinator();
        return true;
    }
    return false;
}

void Core::send_layout(const TelemetryFrame &frame, const size_t index) {
    if (index >= frame.fields.size()) {
        return;
    }
    char line[SerialBus::PAYLOAD_CAPACITY];
    const telemetry::Field &field = frame.fields[index];
    const std::string name = field_name(field);
    const int length = telemetry::format_layout(line, sizeof(line), frame.id, index, name, field.type);
    if (length <= 0 || length >= static_cast<int>(sizeof(line))) {
        echo("warning: layout line of telemetry field \"%s\" is too long", name.c_str());
        return;
    }
    SerialBus *bus = nullptr;
    uint8_t destination = 0;
    if (this->route(frame, this->polled_bus(), bus, destination)) {
        bus->send_layout(destination, line, length);
    } else {
        echo("%s", line);
    }
}

void Core::announce(const TelemetryFrame &frame) {
    const int64_t rate = this->properties.at("telemetry_info_rate")->integer_value();
    for (size_t index = 0; index < frame.fields.size(); ++index) {
        if (rate > 0) {
            this->pending_layout.push_back({frame.id, index});
        } else {
            this->send_layout(frame, index);
        }
    }
}

size_t Core::encode_frame(TelemetryFrame &frame, const unsigned long now, char *line, const size_t capacity) {
    static uint8_t payload[telemetry::MAX_PAYLOAD];
    static uint8_t body[telemetry::MAX_BODY];
    const size_t payload_length = telemetry::pack(frame.fields, payload, sizeof(payload));
    const size_t body_length = telemetry::build_body(frame.id, frame.seq, now, payload, payload_length, body);
    return telemetry::encode_line(body, body_length, line, capacity);
}

void Core::emit_telemetry() {
    const int64_t start = esp_timer_get_time();
    const unsigned long now = millis();
    static char line[telemetry::MAX_LINE];
    SerialBus *const polled = this->telemetry_frames.empty() ? nullptr : this->polled_bus();
    for (auto &frame : this->telemetry_frames) {
        if (frame.interval > 0 && now - frame.last_millis < frame.interval) {
            continue;
        }
        SerialBus *bus = nullptr;
        uint8_t destination = 0;
        if (!this->route(frame, polled, bus, destination)) {
            frame.last_millis = now;
            if (this->encode_frame(frame, now, line, sizeof(line))) {
                echo("%s", line);
            }
            ++frame.seq;
            continue;
        }
        switch (bus->frame_mode()) {
        case 0: {
            // mailbox: the newest state waits for the poll; seq counts frames that left, so gaps stay losses
            const bool pending = frame.stored && bus->frame_pending(frame.id);
            if (frame.stored && !pending) {
                ++frame.seq;
            }
            frame.last_millis = now;
            const size_t length = this->encode_frame(frame, now, line, sizeof(line));
            if (length && bus->store_frame(frame.id, destination, line, length, pending)) {
                frame.stored = true;
            }
            break;
        }
        case 1: {
            // poll-triggered: one frame per poll into the send queue, it leaves with the next poll
            const uint32_t polls = bus->poll_count();
            if (polls == frame.last_poll_count) {
                break;
            }
            frame.last_poll_count = polls;
            frame.last_millis = now;
            const size_t length = this->encode_frame(frame, now, line, sizeof(line));
            if (length && bus->try_send_frame(destination, line, length)) {
                ++frame.seq;
            }
            break;
        }
        default:
            break; // modes 2 and 3: the communication task builds it while answering the poll
        }
    }
    const int64_t rate = this->properties.at("telemetry_info_rate")->integer_value();
    for (int64_t i = 0; i < rate && !this->pending_layout.empty(); ++i) {
        const auto [id, index] = this->pending_layout.front();
        this->pending_layout.pop_front();
        const auto it = std::find_if(this->telemetry_frames.begin(), this->telemetry_frames.end(),
                                     [id = id](const TelemetryFrame &frame) { return frame.id == id; });
        if (it != this->telemetry_frames.end()) {
            this->send_layout(*it, index);
        }
    }
    const int64_t elapsed = esp_timer_get_time() - start;
    this->properties.at("telemetry_us")->set_integer_value(elapsed);
    const Variable_ptr maximum = this->properties.at("telemetry_us_max");
    if (elapsed > maximum->integer_value()) {
        maximum->set_integer_value(elapsed);
    }
}

void Core::build_frames_for_poll(SerialBus *bus, const uint8_t requester, const bool locked) {
    const int64_t wait_start = esp_timer_get_time();
    std::unique_ptr<InterpreterLock> lock; // the main task interprets between these reads
    if (locked) {
        lock = std::make_unique<InterpreterLock>();
    }
    bus->record_poll_lock_wait(esp_timer_get_time() - wait_start);
    const unsigned long now = millis();
    static char line[telemetry::MAX_LINE];
    SerialBus *const polled = this->polled_bus();
    for (auto &frame : this->telemetry_frames) {
        SerialBus *frame_bus = nullptr;
        uint8_t destination = 0;
        if (!this->route(frame, polled, frame_bus, destination) || frame_bus != bus || destination != requester) {
            continue;
        }
        if (frame.interval > 0 && now - frame.last_millis < frame.interval) {
            continue;
        }
        frame.last_millis = now;
        const size_t length = this->encode_frame(frame, now, line, sizeof(line));
        if (length) {
            bus->send_frame_now(requester, line, length);
            ++frame.seq;
        }
    }
}
