#pragma once

#include "../compilation/expression.h"
#include "../compilation/variable.h"
#include <functional>
#include <list>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

class Module;
using Module_ptr = std::shared_ptr<Module>;
using ConstModule_ptr = std::shared_ptr<const Module>;
using MessageHandler = void (*)(const char *line, bool trigger_keep_alive, bool from_expander);
using ModuleFactory = std::function<Module_ptr(const std::string &name,
                                               const std::vector<ConstExpression_ptr> &arguments,
                                               MessageHandler message_handler)>;
using DefaultsFunction = std::function<std::map<std::string, Variable_ptr>()>;

#define REGISTER_MODULE(class_name, factory_fn)                                                 \
    namespace {                                                                                 \
    struct RegisterModule_##class_name {                                                        \
        RegisterModule_##class_name() {                                                         \
            Module::register_module(class_name::TYPE, (factory_fn), &class_name::get_defaults); \
        }                                                                                       \
    } register_module_##class_name;                                                             \
    } // namespace

class Module {
protected:
    std::list<Module_ptr> shadow_modules;
    std::map<std::string, Variable_ptr> properties;
    bool output_on = false;
    bool broadcast = false;

public:
    static bool broadcast_paused;
    const std::string name;

    Module(const std::string name);
    virtual ~Module() = default;
    static void expect(const std::vector<ConstExpression_ptr> arguments, const int num, ...);
    static Module_ptr create(const std::string type,
                             const std::string name,
                             const std::vector<ConstExpression_ptr> arguments,
                             MessageHandler message_handler);
    virtual void step();
    virtual void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments);
    static void register_module(const std::string &type_name, ModuleFactory factory, DefaultsFunction defaults);
    static const std::map<std::string, Variable_ptr> get_module_defaults(const std::string &type_name);
    void call_with_shadows(const std::string method_name, const std::vector<ConstExpression_ptr> arguments);
    virtual std::string get_output() const;
    Variable_ptr get_property(const std::string property_name) const;
    virtual void write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander = false);
    virtual void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data);
};
