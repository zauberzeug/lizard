#include "core.h"
#include "../global.h"
#include "../utils/string_utils.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "esp_ota_ops.h"
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
            const std::string module_name = cut_first_word(element, '.');
            const ConstModule_ptr module = Global::get_module(module_name);
            const std::string method_name = cut_first_word(element, ':');
            const unsigned int precision = element.empty() ? 0 : atoi(element.c_str());
            this->output_list.push_back({module, method_name, precision});
        }
        this->output_on = true;
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
        const Variable_ptr property = element.module->get_property(element.property_name);
        switch (property->type) {
        case boolean:
            pos += sprintf(&output_buffer[pos], "%s", property->boolean_value ? "true" : "false");
            break;
        case integer:
            pos += sprintf(&output_buffer[pos], "%lld", property->integer_value);
            break;
        case number:
            pos += sprintf(&output_buffer[pos], "%.*f", element.precision, property->number_value);
            break;
        case string:
            pos += sprintf(&output_buffer[pos], "\"%s\"", property->string_value.c_str());
            break;
        default:
            throw std::runtime_error("invalid property type");
        }
    }
    return std::string(output_buffer);
}

void Core::keep_alive() {
    this->last_message_millis = millis();
}