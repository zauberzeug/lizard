#include "module.h"
#include "../global.h"
#include "../utils/string_utils.h"
#include "../utils/uart.h"
#include <stdarg.h>
#include <typeinfo>

namespace {

struct ModuleRegistration {
    ModuleFactory factory;
    DefaultsFunction defaults;
};

using ModuleRegistry = std::map<std::string, ModuleRegistration>;

ModuleRegistry &get_registry() {
    static ModuleRegistry registry;
    return registry;
}

} // namespace

bool Module::broadcast_paused = false;

Module::Module(const std::string name) : name(name) {
}

void Module::Module::expect(const std::vector<ConstExpression_ptr> arguments, const int num, ...) {
    if (num >= 0 && arguments.size() != num) {
        throw std::runtime_error("expecting " + std::to_string(num) + " arguments, got " + std::to_string(arguments.size()));
    }
    va_list vl;
    va_start(vl, num);
    for (int i = 0; i < arguments.size(); i++) {
        if ((arguments[i]->type & va_arg(vl, int)) == 0) {
            throw std::runtime_error("type mismatch at argument " + std::to_string(i));
        }
    }
    va_end(vl);
}

Module_ptr Module::create(const std::string type,
                          const std::string name,
                          const std::vector<ConstExpression_ptr> arguments,
                          MessageHandler message_handler) {
    const auto &registry = get_registry();
    const auto it = registry.find(type);
    if (it == registry.end()) {
        throw std::runtime_error("unknown module type \"" + type + "\"");
    }
    return it->second.factory(name, arguments, message_handler);
}

void Module::step() {
    if (this->output_on) {
        const std::string output = this->get_output();
        if (!output.empty()) {
            echo("%s %s", this->name.c_str(), output.c_str());
        }
    }
    if (!Module::broadcast_paused && this->broadcast && !this->properties.empty()) {
        static char buffer[1024];
        int pos = csprintf(buffer, sizeof(buffer), "!!");
        for (auto const &[property_name, property] : this->properties) {
            pos += csprintf(&buffer[pos], sizeof(buffer) - pos, "%s.%s=", this->name.c_str(), property_name.c_str());
            pos += property->print_to_buffer(&buffer[pos], sizeof(buffer) - pos);
            pos += csprintf(&buffer[pos], sizeof(buffer) - pos, ";");
        }
        echo("%s", buffer);
    }
}

void Module::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "mute") {
        Module::expect(arguments, 0);
        this->output_on = false;
    } else if (method_name == "unmute") {
        Module::expect(arguments, 0);
        this->output_on = true;
    } else if (method_name == "broadcast") {
        Module::expect(arguments, 0);
        this->broadcast = true;
    } else if (method_name == "shadow") {
        Module::expect(arguments, 1, identifier);
        std::string target_name = arguments[0]->evaluate_identifier();
        Module_ptr target_module = Global::get_module(target_name);
        if (typeid(*this) != typeid(*target_module)) {
            throw std::runtime_error("shadow module is not of same type");
        }
        if (this != target_module.get()) {
            this->shadow_modules.push_back(target_module);
        }
    } else {
        throw std::runtime_error("unknown method \"" + this->name + "." + method_name + "\"");
    }
}

void Module::call_with_shadows(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    this->call(method_name, arguments);
    for (auto const &module : this->shadow_modules) {
        module->call(method_name, arguments);
    }
}

std::string Module::get_output() const {
    return "";
}

Variable_ptr Module::get_property(const std::string property_name) const {
    if (!this->properties.count(property_name)) {
        throw std::runtime_error("unknown property \"" + property_name + "\"");
    }
    return this->properties.at(property_name);
}

void Module::write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) {
    this->get_property(property_name)->assign(expression);
}

void Module::handle_can_msg(const uint32_t id, const int count, const uint8_t *data) {
    throw std::runtime_error("CAN message handler is not implemented");
}

void Module::register_module(const std::string &type_name, ModuleFactory factory, DefaultsFunction defaults) {
    auto &registry = get_registry();
    if (registry.count(type_name)) {
        throw std::runtime_error("duplicate module registration for \"" + type_name + "\"");
    }
    registry[type_name] = {factory, defaults};
}

const std::map<std::string, Variable_ptr> Module::get_module_defaults(const std::string &type_name) {
    const auto &registry = get_registry();
    const auto it = registry.find(type_name);
    if (it == registry.end()) {
        throw std::runtime_error("No defaults registered for module type \"" + type_name + "\"");
    }
    return it->second.defaults();
}
