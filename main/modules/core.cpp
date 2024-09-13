#include "core.h"
#include "../global.h"
#include "../storage.h"
#include "../utils/ota.h"
#include "../utils/string_utils.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "driver/gpio.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_reg.h"
#include "soc/io_mux_reg.h"
#include <memory>
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
        const esp_app_desc_t *app_desc = esp_ota_get_app_description();
        echo("version: %s", app_desc->version);
    } else if (method_name == "info") {
        Module::expect(arguments, 0);
        const esp_app_desc_t *app_desc = esp_ota_get_app_description();
        echo("lizard version: %s", app_desc->version);
        echo("compile time: %s, %s", app_desc->date, app_desc->time);
        echo("idf version: %s", app_desc->idf_ver);
    } else if (method_name == "print") {
        static char buffer[1024];
        int pos = 0;
        for (auto const &argument : arguments) {
            if (argument != arguments[0]) {
                pos += sprintf(&buffer[pos], " ");
            }
            pos += argument->print_to_buffer(&buffer[pos]);
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
        Module::expect(arguments, 3, string, string, string);
        auto *params = new ota::ota_params_t{
            arguments[0]->evaluate_string(),
            arguments[1]->evaluate_string(),
            arguments[2]->evaluate_string(),
        };
        xTaskCreate(ota::ota_task, "ota_task", 8192, params, 5, nullptr);
    } else if (method_name == "strapping") {
        Module::expect(arguments, 1, integer);
        const gpio_num_t gpio_num = (gpio_num_t)arguments[0]->evaluate_integer();
        if (gpio_num < 0 || gpio_num >= GPIO_NUM_MAX) {
            echo("Invalid GPIO number: %d", gpio_num);
            return;
        }

        int level = gpio_get_level(gpio_num);
        bool output_en = (gpio_set_level(gpio_num, level) == ESP_OK);
        bool input_en = (gpio_get_level(gpio_num) != -1);
        int od_en = (GPIO.pin[gpio_num].pad_driver == 1);

        esp_err_t pullup_result = gpio_set_pull_mode(gpio_num, GPIO_PULLUP_ONLY);
        bool pullup_en = (pullup_result == ESP_OK);
        gpio_set_pull_mode(gpio_num, GPIO_FLOATING);

        esp_err_t pulldown_result = gpio_set_pull_mode(gpio_num, GPIO_PULLDOWN_ONLY);
        bool pulldown_en = (pulldown_result == ESP_OK);
        gpio_set_pull_mode(gpio_num, GPIO_FLOATING);

        gpio_config_t io_conf;
        io_conf.pin_bit_mask = (1ULL << gpio_num);
        gpio_config(&io_conf);

        int intr_type = io_conf.intr_type;

        echo("GPIO[%d]| InputEn: %d| OutputEn: %d| OpenDrain: %d| Pullup: %d| Pulldown: %d| Intr: %d",
             gpio_num, input_en, output_en, od_en, pullup_en, pulldown_en, intr_type);
    } else {
        Module::call(method_name, arguments);
    }
}

std::string Core::get_output() const {
    static char output_buffer[1024];
    int pos = 0;
    for (auto const &element : this->output_list) {
        if (pos > 0) {
            pos += sprintf(&output_buffer[pos], " ");
        }
        const Variable_ptr variable =
            element.module ? element.module->get_property(element.property_name) : Global::get_variable(element.property_name);
        switch (variable->type) {
        case boolean:
            pos += sprintf(&output_buffer[pos], "%s", variable->boolean_value ? "true" : "false");
            break;
        case integer:
            pos += sprintf(&output_buffer[pos], "%lld", variable->integer_value);
            break;
        case number:
            pos += sprintf(&output_buffer[pos], "%.*f", element.precision, variable->number_value);
            break;
        case string:
            pos += sprintf(&output_buffer[pos], "\"%s\"", variable->string_value.c_str());
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