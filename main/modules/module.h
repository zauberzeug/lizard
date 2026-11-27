#pragma once

#include "../compilation/expression.h"
#include "../compilation/variable.h"
#include "../utils/resources.h"
#include <functional>
#include <list>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
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

/// A module's properties by name. A lookup with a string literal remembers the entry it found,
/// so a step neither builds a std::string nor walks the map again; the address of the literal is the key.
class PropertyMap : public std::map<std::string, Variable_ptr> {
private:
    using Base = std::map<std::string, Variable_ptr>;
    // literal -> its entry, which the map never moves; the first lookup of a literal writes here, so lookups by
    // literal belong to the tasks that hold the InterpreterLock (main loop, scheduler), not to a module's own task
    mutable std::vector<std::pair<const char *, Variable_ptr *>> known;

    Variable_ptr *find_remembered(const char *literal) const;

public:
    PropertyMap() = default;
    // a copy starts without remembered entries, they point into the map they came from
    PropertyMap(const Base &other) : Base(other) {}
    PropertyMap(const PropertyMap &other) : Base(other) {}
    PropertyMap &operator=(const Base &other) {
        this->known.clear();
        Base::operator=(other);
        return *this;
    }
    PropertyMap &operator=(const PropertyMap &other) {
        return *this = static_cast<const Base &>(other);
    }

    // the remembered entries point into the map, so nothing may remove an entry or exchange the content
    template <typename... Args>
    void erase(Args &&...) = delete;
    void clear() = delete;
    template <typename... Args>
    void swap(Args &&...) = delete;
    template <typename... Args>
    void extract(Args &&...) = delete;
    template <typename... Args>
    void merge(Args &&...) = delete;

    using Base::at;
    using Base::operator[];

    /// The entry of a literal name, or nullptr if there is none.
    /// The name must be a string literal or a const array with static storage duration:
    /// a local array can share its address with another name later.
    template <typename Char, size_t N>
    Variable_ptr *find_literal(Char (&literal)[N]) const {
        static_assert(std::is_same_v<Char, const char>, "only a name that cannot change can be remembered by its address");
        return this->find_remembered(literal);
    }

    template <typename Char, size_t N>
    Variable_ptr &at(Char (&literal)[N]) {
        Variable_ptr *const entry = this->find_literal(literal);
        if (entry == nullptr) {
            throw std::out_of_range(literal);
        }
        return *entry;
    }

    template <typename Char, size_t N>
    const Variable_ptr &at(Char (&literal)[N]) const {
        return const_cast<PropertyMap *>(this)->at(literal);
    }

    template <typename Char, size_t N>
    Variable_ptr &operator[](Char (&literal)[N]) {
        Variable_ptr *const entry = this->find_literal(literal);
        return entry != nullptr ? *entry : Base::operator[](literal);
    }
};

class Module {
private:
    Variable_ptr enabled_property; // cached by get_enabled_property()

    /// The `enabled` property; throws a runtime_error for modules that do not declare one.
    const Variable_ptr &get_enabled_property();

protected:
    std::list<Module_ptr> shadow_modules;
    // Only ever add to this map, never replace a variable under an existing name: compiled property expressions,
    // core.output fields, telemetry frames and enabled_property keep the Variable_ptr they found once.
    PropertyMap properties;
    bool output_on = false;
    bool broadcast = false;
    bool enabled = true; // last value applied to the hardware; `sync_enabled()` edge-detects direct property writes against it

    /// Apply a direct write to the `enabled` property by calling enable()/disable(); call from step().
    void sync_enabled();

    /// Module-specific part of enable()/disable(). Both hooks run while `enabled` still holds the
    /// old value, and the flags are committed only after the hook returned, so a hook that throws
    /// leaves the transition pending and `sync_enabled()` retries it on the next step.
    /// Hence `do_disable()` may still use methods gated on `enabled`, but `do_enable()` may not.
    virtual void do_enable() {}
    virtual void do_disable() {}

public:
    static bool broadcast_paused;
    const std::string name;

protected:
    resources::Claims claims; // the pins and peripherals of this module, released when it is destroyed; after `name`, which it refers to

public:
    Module(const std::string name);
    virtual ~Module() = default;
    static void expect(const std::vector<ConstExpression_ptr> &arguments, const int num, ...);
    static Module_ptr create(const std::string type,
                             const std::string name,
                             const std::vector<ConstExpression_ptr> arguments,
                             MessageHandler message_handler);
    virtual void step();
    virtual void call(const std::string &method_name, const std::vector<ConstExpression_ptr> &arguments);
    /// Switch the module on/off and keep the `enabled` member and property in sync.
    /// Throws for modules that do not declare an `enabled` property.
    virtual void enable();
    virtual void disable();
    static void register_module(const std::string &type_name, ModuleFactory factory, DefaultsFunction defaults);
    static const std::map<std::string, Variable_ptr> get_module_defaults(const std::string &type_name);
    void call_with_shadows(const std::string &method_name, const std::vector<ConstExpression_ptr> &arguments);
    virtual std::string get_output() const;
    const Variable_ptr &get_property(const std::string &property_name) const;
    template <typename Char, size_t N>
    const Variable_ptr &get_property(Char (&property_name)[N]) const {
        const Variable_ptr *const variable = this->properties.find_literal(property_name);
        if (variable == nullptr) {
            throw std::runtime_error(std::string("unknown property \"") + property_name + "\"");
        }
        return *variable;
    }
    virtual void write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander = false);
    /// Declare a property from Lizard code like `float arm.motor.position = 0.0`; the default implementation throws.
    virtual void declare_property(const std::string &property_name, const Variable_ptr &variable);
    virtual void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data);
};
