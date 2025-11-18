#pragma once

#include "../compilation/expression.h"
#include "../compilation/variable.h"
#include <functional>
#include <list>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#define REGISTER_MODULE_DEFAULTS(module_name)                                   \
    namespace {                                                                 \
    struct RegisterDefaults {                                                   \
        RegisterDefaults() {                                                    \
            Module::register_defaults(#module_name, module_name::get_defaults); \
        }                                                                       \
    } register_defaults;                                                        \
    } // namespace

enum ModuleType {
    bluetooth,
    core,
    expander,
    plexus_expander,
    input,
    output,
    pwm_output,
    mcp23017,
    imu,
    can,
    serial,
    odrive_motor,
    odrive_wheels,
    rmd_motor,
    rmd_pair,
    roboclaw,
    roboclaw_motor,
    roboclaw_wheels,
    stepper_motor,
    motor_axis,
    canopen_motor,
    canopen_master,
    d1_motor,
    dunker_motor,
    dunker_wheels,
    analog,
    analog_unit,
    temperature_sensor,
    proxy,
};

class Module;
using Module_ptr = std::shared_ptr<Module>;
using ConstModule_ptr = std::shared_ptr<const Module>;
using MessageHandler = void (*)(const char *line, bool trigger_keep_alive, bool from_expander);
using DefaultsFunction = std::function<std::map<std::string, Variable_ptr>()>;
using DefaultsRegistry = std::map<std::string, DefaultsFunction>;

class Module {
private:
    std::list<Module_ptr> shadow_modules;
    static DefaultsRegistry &get_defaults_registry();

protected:
    std::map<std::string, Variable_ptr> properties;
    bool output_on = false;
    bool broadcast = false;

public:
    const ModuleType type;
    const std::string name;

    Module(const ModuleType type, const std::string name);
    static void expect(const std::vector<ConstExpression_ptr> arguments, const int num, ...);
    static Module_ptr create(const std::string type,
                             const std::string name,
                             const std::vector<ConstExpression_ptr> arguments,
                             MessageHandler message_handler);
    virtual void step();
    virtual void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments);
    static void register_defaults(const std::string &type_name, DefaultsFunction defaults_function);
    static const std::map<std::string, Variable_ptr> get_module_defaults(const std::string &type_name);
    void call_with_shadows(const std::string method_name, const std::vector<ConstExpression_ptr> arguments);
    virtual std::string get_output() const;
    Variable_ptr get_property(const std::string property_name) const;
    virtual void write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander = false);
    virtual void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data);
};
