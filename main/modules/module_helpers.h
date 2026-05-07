#pragma once

#include "../compilation/expression.h"
#include "../global.h"
#include "module.h"
#include "driver/gpio.h"
#include <memory>
#include <stdexcept>
#include <string>

#ifdef CONFIG_IDF_TARGET_ESP32S3
#define DEFAULT_I2C_SDA_PIN GPIO_NUM_8
#define DEFAULT_I2C_SCL_PIN GPIO_NUM_9
#else
#define DEFAULT_I2C_SDA_PIN GPIO_NUM_21
#define DEFAULT_I2C_SCL_PIN GPIO_NUM_22
#endif

template <typename M>
inline std::shared_ptr<M> get_module_argument(const ConstExpression_ptr &arg, const std::string &type) {
    const std::string name = arg->evaluate_identifier();
    const Module_ptr module = Global::get_module(name);
    if (module->type != type && module->type != "Proxy") {
        throw std::runtime_error("module \"" + name + "\" is no " + type);
    }
    return std::static_pointer_cast<M>(module);
}
