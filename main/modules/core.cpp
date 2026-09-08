#include "core.h"
#include "../global.h"
#include "../storage.h"
#include "../utils/bus_backup.h"
#include "../utils/frame.h"
#include "../utils/scheduler.h"
#include "../utils/string_utils.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "driver/gpio.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/gpio_hal.h"
#include "soc/io_mux_reg.h"
#include "soc/soc.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <stdlib.h>

Core::Core(const std::string name) : Module(name) {
    this->properties["debug"] = std::make_shared<BooleanVariable>(false);
    this->properties["millis"] = std::make_shared<IntegerVariable>();
    this->properties["heap"] = std::make_shared<IntegerVariable>();
    this->properties["tx_us"] = std::make_shared<IntegerVariable>();
    this->properties["tx_us_max"] = std::make_shared<IntegerVariable>();
    this->properties["last_message_age"] = std::make_shared<IntegerVariable>();
}

void Core::step() {
    this->properties.at("millis")->integer_value = millis();
    this->properties.at("heap")->integer_value = xPortGetFreeHeapSize();
    this->properties.at("last_message_age")->integer_value = millis_since(this->last_message_millis);
    const unsigned long start = micros();
    Module::step();
    const unsigned long now = millis();
    for (auto &frame : this->frames) {
        if (now - frame.last_millis >= frame.interval) {
            frame.last_millis = now;
            this->emit_frame(frame, now);
        }
    }
    // time spent formatting and writing telemetry this tick (text line and frames)
    const long long tx_us = micros() - start;
    this->properties.at("tx_us")->integer_value = tx_us;
    if (tx_us > this->properties.at("tx_us_max")->integer_value) {
        this->properties.at("tx_us_max")->integer_value = tx_us;
    }
}

void Core::emit_frame(frame_t &frame, unsigned long now) const {
    static uint8_t payload[frame::MAX_PAYLOAD];
    size_t pos = 0;
    size_t bit = 0;
    uint8_t bits[16] = {0};
    for (auto const &field : frame.fields) {
        const Variable_ptr variable =
            field.module ? field.module->get_property(field.property_name) : Global::get_variable(field.property_name);
        double value = 0;
        switch (variable->type) {
        case boolean:
            value = variable->boolean_value ? 1 : 0;
            break;
        case integer:
            value = static_cast<double>(variable->integer_value);
            break;
        case number:
            value = variable->number_value;
            break;
        default:
            throw std::runtime_error("unsupported frame field type");
        }
        if (field.type == '?') {
            if (value != 0) {
                bits[bit / 8] |= 1 << (bit % 8);
            }
            ++bit;
            continue;
        }
        const double scaled = value * field.scale;
        union {
            int8_t b;
            uint8_t B;
            int16_t h;
            uint16_t H;
            int32_t i;
            uint32_t I;
            float f;
            uint8_t raw[4];
        } u;
        size_t size = 0;
        switch (field.type) {
        case 'b':
            u.b = static_cast<int8_t>(scaled);
            size = 1;
            break;
        case 'B':
            u.B = static_cast<uint8_t>(scaled);
            size = 1;
            break;
        case 'h':
            u.h = static_cast<int16_t>(scaled);
            size = 2;
            break;
        case 'H':
            u.H = static_cast<uint16_t>(scaled);
            size = 2;
            break;
        case 'i':
            u.i = static_cast<int32_t>(scaled);
            size = 4;
            break;
        case 'I':
            u.I = static_cast<uint32_t>(scaled);
            size = 4;
            break;
        case 'f':
            u.f = static_cast<float>(scaled);
            size = 4;
            break;
        default:
            throw std::runtime_error("unknown frame field type");
        }
        if (pos + size > sizeof(payload)) {
            throw std::runtime_error("frame payload too large");
        }
        memcpy(&payload[pos], u.raw, size);
        pos += size;
    }
    const size_t bit_bytes = (bit + 7) / 8;
    if (pos + bit_bytes > sizeof(payload)) {
        throw std::runtime_error("frame payload too large");
    }
    memcpy(&payload[pos], bits, bit_bytes);
    pos += bit_bytes;
    frame::write(0, frame.id, frame.seq++, now, payload, pos);
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
    } else if (method_name == "frame") {
        // frame(id, "field[:type[scale]] ...", interval_ms); types ? b B h H i I f, scale = decimal digits
        Module::expect(arguments, 3, integer, string, integer);
        frame_t frame{static_cast<uint8_t>(arguments[0]->evaluate_integer()),
                      static_cast<unsigned long>(arguments[2]->evaluate_integer()),
                      0,
                      0,
                      {}};
        std::string format = arguments[1]->evaluate_string();
        while (!format.empty()) {
            std::string element = cut_first_word(format);
            std::string name = cut_first_word(element, ':');
            ConstModule_ptr module = nullptr;
            std::string property_name = name;
            if (name.find('.') != std::string::npos) {
                const std::string module_name = cut_first_word(name, '.');
                module = Global::get_module(module_name);
                property_name = name;
            }
            char type = 0;
            double scale = 1;
            if (!element.empty()) {
                type = element[0];
                if (element.size() > 1) {
                    scale = pow(10, atoi(element.c_str() + 1));
                }
            } else {
                const Variable_ptr variable = module ? module->get_property(property_name) : Global::get_variable(property_name);
                type = variable->type == boolean ? '?' : variable->type == integer ? 'i'
                                                                                   : 'f';
            }
            frame.fields.push_back({module, property_name, type, scale});
        }
        this->frames.erase(std::remove_if(this->frames.begin(), this->frames.end(),
                                          [&](const frame_t &f) { return f.id == frame.id; }),
                           this->frames.end());
        this->frames.push_back(frame);
    } else if (method_name == "frame_clear") {
        Module::expect(arguments, 0);
        this->frames.clear();
        this->properties.at("tx_us_max")->integer_value = 0;
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
    } else {
        Module::call(method_name, arguments);
    }
}

std::string Core::get_output() const {
    static char output_buffer[1024];
    int pos = 0;
    for (auto const &element : this->output_list) {
        if (pos > 0) {
            pos += csprintf(&output_buffer[pos], sizeof(output_buffer) - pos, " ");
        }
        const Variable_ptr variable =
            element.module ? element.module->get_property(element.property_name) : Global::get_variable(element.property_name);
        switch (variable->type) {
        case boolean:
            pos += csprintf(&output_buffer[pos], sizeof(output_buffer) - pos, "%s", variable->boolean_value ? "true" : "false");
            break;
        case integer:
            pos += csprintf(&output_buffer[pos], sizeof(output_buffer) - pos, "%lld", variable->integer_value);
            break;
        case number:
            pos += csprintf(&output_buffer[pos], sizeof(output_buffer) - pos, "%.*f", element.precision, variable->number_value);
            break;
        case string:
            pos += csprintf(&output_buffer[pos], sizeof(output_buffer) - pos, "\"%s\"", variable->string_value.c_str());
            break;
        default:
            throw std::runtime_error("invalid type");
        }
    }
    return std::string(output_buffer);
}

void Core::keep_alive() {
    this->last_message_millis = millis();
}
