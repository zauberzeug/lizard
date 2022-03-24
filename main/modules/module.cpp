#include "module.h"
#include "../global.h"
#include "../utils/uart.h"
#include "bluetooth.h"
#include "can.h"
#include "canopen_master.h"
#include "canopen_motor.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pcnt.h"
#include "expander.h"
#include "input.h"
#include "linear_motor.h"
#include "odrive_motor.h"
#include "odrive_wheels.h"
#include "output.h"
#include "rmd_motor.h"
#include "roboclaw.h"
#include "roboclaw_motor.h"
#include "serial.h"
#include "stepper_motor.h"
#include <stdarg.h>

Module::Module(const ModuleType type, const std::string name) : type(type), name(name) {
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

template <typename M>
static std::shared_ptr<M> get_module_paramter(const ConstExpression_ptr &arg, ModuleType type, const std::string &type_name) {
    const std::string name = arg->evaluate_identifier();
    Module_ptr module = Global::get_module(name);
    if (module->type != type) {
        throw std::runtime_error("module \"" + name + "\" is no " + type_name);
    }

    const std::shared_ptr<M> typed_module = std::static_pointer_cast<M>(module);
    return typed_module;
}

Module_ptr Module::create(const std::string type,
                          const std::string name,
                          const std::vector<ConstExpression_ptr> arguments,
                          void (*message_handler)(const char *)) {
    if (type == "Core") {
        throw std::runtime_error("creating another core module is forbidden");
    } else if (type == "Expander") {
        Module::expect(arguments, 3, identifier, integer, integer);
        std::string serial_name = arguments[0]->evaluate_identifier();
        Module_ptr module = Global::get_module(serial_name);
        if (module->type != serial) {
            throw std::runtime_error("module \"" + serial_name + "\" is no serial connection");
        }
        const ConstSerial_ptr serial = std::static_pointer_cast<const Serial>(module);
        const gpio_num_t boot_pin = (gpio_num_t)arguments[1]->evaluate_integer();
        const gpio_num_t enable_pin = (gpio_num_t)arguments[2]->evaluate_integer();
        return std::make_shared<Expander>(name, serial, boot_pin, enable_pin, message_handler);
    } else if (type == "Bluetooth") {
        Module::expect(arguments, 1, string);
        std::string device_name = arguments[0]->evaluate_string();
        Bluetooth_ptr bluetooth = std::make_shared<Bluetooth>(name, device_name, message_handler);
        return bluetooth;
    } else if (type == "Output") {
        Module::expect(arguments, 1, integer);
        return std::make_shared<Output>(name, (gpio_num_t)arguments[0]->evaluate_integer());
    } else if (type == "Input") {
        Module::expect(arguments, 1, integer);
        return std::make_shared<Input>(name, (gpio_num_t)arguments[0]->evaluate_integer());
    } else if (type == "Can") {
        Module::expect(arguments, 3, integer, integer, integer, integer);
        gpio_num_t rx_pin = (gpio_num_t)arguments[0]->evaluate_integer();
        gpio_num_t tx_pin = (gpio_num_t)arguments[1]->evaluate_integer();
        long baud_rate = arguments[2]->evaluate_integer();
        return std::make_shared<Can>(name, rx_pin, tx_pin, baud_rate);
    } else if (type == "LinearMotor") {
        Module::expect(arguments, 4, integer, integer, integer, integer);
        gpio_num_t move_in = (gpio_num_t)arguments[0]->evaluate_integer();
        gpio_num_t move_out = (gpio_num_t)arguments[1]->evaluate_integer();
        gpio_num_t end_in = (gpio_num_t)arguments[2]->evaluate_integer();
        gpio_num_t end_out = (gpio_num_t)arguments[3]->evaluate_integer();
        return std::make_shared<LinearMotor>(name, move_in, move_out, end_in, end_out);
    } else if (type == "ODriveMotor") {
        Module::expect(arguments, 2, identifier, integer);
        std::string can_name = arguments[0]->evaluate_identifier();
        Module_ptr module = Global::get_module(can_name);
        if (module->type != can) {
            throw std::runtime_error("module \"" + can_name + "\" is no can connection");
        }
        const Can_ptr can = std::static_pointer_cast<Can>(module);
        uint32_t can_id = arguments[1]->evaluate_integer();
        ODriveMotor_ptr odrive_motor = std::make_shared<ODriveMotor>(name, can, can_id);
        odrive_motor->subscribe_to_can();
        return odrive_motor;
    } else if (type == "ODriveWheels") {
        Module::expect(arguments, 2, identifier, identifier);
        std::string left_name = arguments[0]->evaluate_identifier();
        std::string right_name = arguments[1]->evaluate_identifier();
        Module_ptr left_module = Global::get_module(left_name);
        Module_ptr right_module = Global::get_module(right_name);
        if (left_module->type != odrive_motor) {
            throw std::runtime_error("module \"" + left_name + "\" is no ODrive motor");
        }
        if (right_module->type != odrive_motor) {
            throw std::runtime_error("module \"" + right_name + "\" is no ODrive motor");
        }
        const ODriveMotor_ptr left_motor = std::static_pointer_cast<ODriveMotor>(left_module);
        const ODriveMotor_ptr right_motor = std::static_pointer_cast<ODriveMotor>(right_module);
        return std::make_shared<ODriveWheels>(name, left_motor, right_motor);
    } else if (type == "RmdMotor") {
        Module::expect(arguments, 2, identifier, integer);
        std::string can_name = arguments[0]->evaluate_identifier();
        Module_ptr module = Global::get_module(can_name);
        if (module->type != can) {
            throw std::runtime_error("module \"" + can_name + "\" is no can connection");
        }
        const Can_ptr can = std::static_pointer_cast<Can>(module);
        uint8_t motor_id = arguments[1]->evaluate_integer();
        RmdMotor_ptr rmd_motor = std::make_shared<RmdMotor>(name, can, motor_id);
        rmd_motor->subscribe_to_can();
        return rmd_motor;
    } else if (type == "Serial") {
        Module::expect(arguments, 4, integer, integer, integer, integer);
        gpio_num_t rx_pin = (gpio_num_t)arguments[0]->evaluate_integer();
        gpio_num_t tx_pin = (gpio_num_t)arguments[1]->evaluate_integer();
        long baud_rate = arguments[2]->evaluate_integer();
        gpio_port_t uart_num = (gpio_port_t)arguments[3]->evaluate_integer();
        return std::make_shared<Serial>(name, rx_pin, tx_pin, baud_rate, uart_num);
    } else if (type == "RoboClaw") {
        Module::expect(arguments, 2, identifier, integer);
        std::string serial_name = arguments[0]->evaluate_identifier();
        Module_ptr module = Global::get_module(serial_name);
        if (module->type != serial) {
            throw std::runtime_error("module \"" + serial_name + "\" is no serial connection");
        }
        const ConstSerial_ptr serial = std::static_pointer_cast<const Serial>(module);
        uint8_t address = arguments[1]->evaluate_integer();
        return std::make_shared<RoboClaw>(name, serial, address);
    } else if (type == "RoboClawMotor") {
        Module::expect(arguments, 2, identifier, integer);
        std::string roboclaw_name = arguments[0]->evaluate_identifier();
        Module_ptr module = Global::get_module(roboclaw_name);
        if (module->type != roboclaw) {
            throw std::runtime_error("module \"" + roboclaw_name + "\" is no RoboClaw");
        }
        const RoboClaw_ptr roboclaw = std::static_pointer_cast<RoboClaw>(module);
        int64_t motor_number = arguments[1]->evaluate_integer();
        return std::make_shared<RoboClawMotor>(name, roboclaw, motor_number);
    } else if (type == "StepperMotor") {
        if (arguments.size() < 2 || arguments.size() > 6) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, integer, integer, integer, integer, integer, integer);
        gpio_num_t step_pin = (gpio_num_t)arguments[0]->evaluate_integer();
        gpio_num_t dir_pin = (gpio_num_t)arguments[1]->evaluate_integer();
        pcnt_unit_t pcnt_unit = arguments.size() > 2 ? (pcnt_unit_t)arguments[2]->evaluate_integer() : PCNT_UNIT_0;
        pcnt_channel_t pcnt_channel = arguments.size() > 3 ? (pcnt_channel_t)arguments[3]->evaluate_integer() : PCNT_CHANNEL_0;
        ledc_timer_t ledc_timer = arguments.size() > 4 ? (ledc_timer_t)arguments[4]->evaluate_integer() : LEDC_TIMER_0;
        ledc_channel_t ledc_channel = arguments.size() > 5 ? (ledc_channel_t)arguments[5]->evaluate_integer() : LEDC_CHANNEL_0;
        return std::make_shared<StepperMotor>(name, step_pin, dir_pin, pcnt_unit, pcnt_channel, ledc_timer, ledc_channel);
    } else if (type == "CanOpenMotor") {
        Module::expect(arguments, 2, identifier, integer);
        const Can_ptr can_module = get_module_paramter<Can>(arguments[0], can, "can connection");
        const int64_t node_id = arguments[1]->evaluate_integer();
        CanOpenMotor_ptr motor = std::make_shared<CanOpenMotor>(name, can_module, node_id);
        motor->subscribe_to_can();
        return motor;
    } else if (type == "CanOpenMaster") {
        Module::expect(arguments, 1, identifier);
        const Can_ptr can_module = get_module_paramter<Can>(arguments[0], can, "can connection");
        CanOpenMaster_ptr master = std::make_shared<CanOpenMaster>(name, can_module);
        return master;
    } else {
        throw std::runtime_error("unknown module type \"" + type + "\"");
    }
}

void Module::step() {
    if (this->output_on) {
        const std::string output = this->get_output();
        if (!output.empty()) {
            echo("%s %s", this->name.c_str(), output.c_str());
        }
    }
    if (this->broadcast && !this->properties.empty()) {
        static char buffer[1024];
        int pos = sprintf(buffer, "!!");
        for (auto const &[property_name, property] : this->properties) {
            pos += sprintf(&buffer[pos], "%s.%s=", this->name.c_str(), property_name.c_str());
            pos += property->print_to_buffer(&buffer[pos]);
            pos += sprintf(&buffer[pos], ";");
        }
        echo(buffer);
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
        if (this->type != target_module->type) {
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

void Module::write_property(const std::string property_name, const ConstExpression_ptr expression) {
    this->get_property(property_name)->assign(expression);
}

void Module::handle_can_msg(const uint32_t id, const int count, const uint8_t *data) {
    throw std::runtime_error("CAN message handler is not implemented");
}
