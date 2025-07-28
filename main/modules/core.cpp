#include "core.h"
#include "../global.h"
#include "../storage.h"
#include "../utils/addressing.h"
#include "../utils/ota.h"
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
#include <memory>
#include <stdexcept>
#include <stdlib.h>

Core::Core(const std::string name) : Module(core, name) {
    this->properties["debug"] = std::make_shared<BooleanVariable>(false);
    this->properties["millis"] = std::make_shared<IntegerVariable>();
    this->properties["heap"] = std::make_shared<IntegerVariable>();
    this->properties["last_message_age"] = std::make_shared<IntegerVariable>();
}

void Core::step() {
    this->properties.at("millis")->integer_value = millis();
    this->properties.at("heap")->integer_value = xPortGetFreeHeapSize();
    this->properties.at("last_message_age")->integer_value = millis_since(this->last_message_millis);
    Module::step();
}

void Core::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "restart") {
        Module::expect(arguments, 0);
        esp_restart();
    } else if (method_name == "version") {
        const esp_app_desc_t *app_desc = esp_app_get_description();
        echo("version: %s", app_desc->version);
    } else if (method_name == "info") {
        Module::expect(arguments, 0);
        const esp_app_desc_t *app_desc = esp_app_get_description();
        echo("lizard version: %s", app_desc->version);
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
        echo(buffer);
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
    } else if (method_name == "startup_checksum") {
        uint16_t checksum = 0;
        for (char const &c : Storage::startup) {
            checksum += c;
        }
        echo("checksum: %04x", checksum);
    } else if (method_name == "ota") {
        Module::expect(arguments, 0);
        echo("Starting automatic UART OTA...");

        if (!ota::perform_automatic_ota("core")) {
            echo("UART OTA failed");
        }
    } else if (method_name == "ota_bridge_start") {
        Module::expect(arguments, 0);
        echo("Starting UART bridge...");
        ota::start_ota_bridge_task();
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
        // Register 4.13. GPIO_STRAP_REG (0x0038)
        // https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf
        switch (gpio_num) {
        case GPIO_NUM_0:
            echo("Strapping GPIO0: %d", (strapping_reg & BIT(0)) ? 1 : 0);
            break;
        case GPIO_NUM_2:
            echo("Strapping GPIO2: %d", (strapping_reg & BIT(1)) ? 1 : 0);
            break;
        case GPIO_NUM_4:
            echo("Strapping GPIO4: %d", (strapping_reg & BIT(5)) ? 1 : 0);
            break;
        case GPIO_NUM_5:
            echo("Strapping GPIO5: %d", (strapping_reg & BIT(4)) ? 1 : 0);
            break;
        case GPIO_NUM_12:
            echo("Strapping GPIO12 (MTDI): %d", (strapping_reg & BIT(3)) ? 1 : 0);
            break;
        case GPIO_NUM_15:
            echo("Strapping GPIO15 (MTDO): %d", (strapping_reg & BIT(2)) ? 1 : 0);
            break;
        default:
            echo("Not a strapping pin");
            break;
        }
    } else if (method_name == "run_step") {
        Module::expect(arguments, 0);
        run_step();
    } else if (method_name == "pe") {
        echo("Debug: external status: %d", get_uart_external_mode());
    } else if (method_name == "ee_on") { // Debug function remove later
        Module::expect(arguments, 0);
        activate_uart_external_mode();
    } else if (method_name == "ee_off") { // Debug function remove later
        Module::expect(arguments, 0);
        deactivate_uart_external_mode();
    } else if (method_name == "set_device_id") {
        Module::expect(arguments, 1, integer);
        uint8_t id = arguments[0]->evaluate_integer();
        if (id > 9) {
            throw std::runtime_error("expander id must be between 0 and 99");
        }
        char id_str = static_cast<char>('0' + id);
        set_uart_expander_id(id_str);
        Storage::put_device_id(id_str);
        echo("Device ID set to %c", id_str);
    } else if (method_name == "get_device_id") {
        Module::expect(arguments, 0);
        char id = get_uart_expander_id();
        echo("Device ID: %c", id);
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

void Core::run_step() {
    if (!get_uart_external_mode()) {
        echo("Debug: Not in external mode, skipping step");
        return;
    }

    // Run one step of all modules
    for (auto const &[module_name, module] : Global::modules) {
        if (module->name != "core") {
            try {
                module->step();
            } catch (const std::runtime_error &e) {
                echo("error in module \"%s\": %s", module->name.c_str(), e.what());
            }
        }
    }

    // Run core module last
    try {
        this->step();
    } catch (const std::runtime_error &e) {
        echo("error in core module: %s", e.what());
    }

    echo("__step_done__");
}