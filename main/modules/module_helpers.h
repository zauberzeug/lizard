#pragma once

#include "../compilation/expression.h"
#include "../global.h"
#include "driver/gpio.h"
#include "module.h"
#include <memory>
#include <stdexcept>
#include <string>

// The strapping pins are used by the expander's pre-flash check; since an expander is flashed with our own
// running partition, it is always the same target as we are and compile-time constants are sufficient.
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define DEFAULT_I2C_SDA_PIN GPIO_NUM_8
#define DEFAULT_I2C_SCL_PIN GPIO_NUM_9
#define BOOT_MODE_PIN 46
#define FLASH_VOLTAGE_PIN 45
#elif defined(CONFIG_IDF_TARGET_ESP32)
#define DEFAULT_I2C_SDA_PIN GPIO_NUM_21
#define DEFAULT_I2C_SCL_PIN GPIO_NUM_22
#define BOOT_MODE_PIN 2
#define FLASH_VOLTAGE_PIN 12
#else
#error "Unsupported IDF target: define its default and strapping pins here."
#endif

template <typename M>
inline std::shared_ptr<M> get_module_argument(const ConstExpression_ptr &arg) {
    const std::string name = arg->evaluate_identifier();
    const Module_ptr module = Global::get_module(name);
    if (const auto typed = std::dynamic_pointer_cast<M>(module)) {
        return typed;
    }
    // Note that this also rejects proxies of expander modules: a proxy is not an M in
    // the C++ type system, so casting it to M would be UB (#233).
    throw std::runtime_error("module \"" + name + "\" is no " + M::TYPE);
}
