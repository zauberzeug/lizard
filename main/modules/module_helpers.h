#pragma once

#include "../compilation/expression.h"
#include "../global.h"
#include "driver/gpio.h"
#include "module.h"
#include "proxy.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

#ifdef CONFIG_IDF_TARGET_ESP32S3
#define DEFAULT_I2C_SDA_PIN GPIO_NUM_8
#define DEFAULT_I2C_SCL_PIN GPIO_NUM_9
#else
#define DEFAULT_I2C_SDA_PIN GPIO_NUM_21
#define DEFAULT_I2C_SCL_PIN GPIO_NUM_22
#endif

template <typename M, typename Held = M>
inline std::shared_ptr<Held> get_module_argument(const ConstExpression_ptr &arg) {
    const std::string name = arg->evaluate_identifier();
    const Module_ptr module = Global::get_module(name);
    if (const auto typed = std::dynamic_pointer_cast<M>(module)) {
        return typed;
    }
    // A proxy is not an M in the C++ type system, so casting it to M is UB (#233).
    // But when the caller holds it only as a base (Held, e.g. Module), a proxy standing
    // for a remote M is safe: validate it by the remote type name, return it as that base.
    if constexpr (!std::is_same_v<Held, M>) {
        if (const auto proxy = std::dynamic_pointer_cast<Proxy>(module)) {
            if (proxy->module_type == M::TYPE) {
                return proxy;
            }
        }
    }
    throw std::runtime_error("module \"" + name + "\" is no " + M::TYPE);
}
