#pragma once

#include "../compilation/expression.h"
#include "../compilation/variable.h"
#include <list>
#include <map>
#include <string>
#include <vector>

enum ModuleType {
    bluetooth,
    core,
    expander,
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
    proxy,
};

class Module;
using Module_ptr = std::shared_ptr<Module>;
using ConstModule_ptr = std::shared_ptr<const Module>;
using MessageHandler = void (*)(const char *line, bool trigger_keep_alive, bool from_expander);

class Module {
private:
    std::list<Module_ptr> shadow_modules;

protected:
    std::map<std::string, Variable_ptr> properties;
    bool output_on = false;
    bool broadcast = false;

public:
    using DefaultsGetter = const std::map<std::string, Variable_ptr> &(*)();
    static std::map<std::string, DefaultsGetter> defaults_getters;

    static void register_defaults(const std::string &type_name,
                                  const std::map<std::string, Variable_ptr> &props,
                                  DefaultsGetter getter);

    static const std::map<std::string, Variable_ptr> &get_module_defaults(const std::string &type_name);

    static std::map<std::string, std::map<std::string, Variable_ptr>> default_properties;

    const ModuleType type;
    const std::string name;
    std::map<std::string, Variable_ptr> get_default_properties() const;

    Module(const ModuleType type, const std::string name);
    static void expect(const std::vector<ConstExpression_ptr> arguments, const int num, ...);
    static Module_ptr create(const std::string type,
                             const std::string name,
                             const std::vector<ConstExpression_ptr> arguments,
                             MessageHandler message_handler);
    virtual void step();
    virtual void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments);
    void call_with_shadows(const std::string method_name, const std::vector<ConstExpression_ptr> arguments);
    virtual std::string get_output() const;
    Variable_ptr get_property(const std::string property_name) const;
    virtual void write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander = false);
    virtual void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data);
};
