#include "module.h"
#include "../global.h"
#include "../utils/string_utils.h"
#include "../utils/uart.h"
#include "analog.h"
#include "analog_dual.h"
#include "analog_unit.h"
#include "bluetooth.h"
#include "can.h"
#include "canopen_master.h"
#include "canopen_motor.h"
#include "d1_motor.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pcnt.h"
#include "dunker_motor.h"
#include "dunker_wheels.h"
#include "expander.h"
#include "imu.h"
#include "input.h"
#include "linear_motor.h"
#include "mcp23017.h"
#include "motor_axis.h"
#include "odrive_motor.h"
#include "odrive_wheels.h"
#include "output.h"
#include "pwm_output.h"
#include "rmd_motor.h"
#include "rmd_pair.h"
#include "roboclaw.h"
#include "roboclaw_motor.h"
#include "roboclaw_wheels.h"
#include "serial.h"
#include "stepper_motor.h"
#include <stdarg.h>

#ifdef CONFIG_IDF_TARGET_ESP32S3
#define DEFAULT_SDA_PIN GPIO_NUM_8
#define DEFAULT_SCL_PIN GPIO_NUM_9
#else
#define DEFAULT_SDA_PIN GPIO_NUM_21
#define DEFAULT_SCL_PIN GPIO_NUM_22
#endif

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
    if (module->type != type && module->type != proxy) {
        throw std::runtime_error("module \"" + name + "\" is no " + type_name);
    }

    const std::shared_ptr<M> typed_module = std::static_pointer_cast<M>(module);
    return typed_module;
}

Module_ptr Module::create(const std::string type,
                          const std::string name,
                          const std::vector<ConstExpression_ptr> arguments,
                          MessageHandler message_handler) {
    if (type == "Core") {
        throw std::runtime_error("creating another core module is forbidden");
    } else if (type == "Expander") {
        if (arguments.size() != 1 && arguments.size() != 3) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, identifier, integer, integer);
        std::string serial_name = arguments[0]->evaluate_identifier();
        Module_ptr module = Global::get_module(serial_name);
        if (module->type != serial) {
            throw std::runtime_error("module \"" + serial_name + "\" is no serial connection");
        }
        const ConstSerial_ptr serial = std::static_pointer_cast<const Serial>(module);
        const gpio_num_t boot_pin = arguments.size() > 1 ? (gpio_num_t)arguments[1]->evaluate_integer() : GPIO_NUM_NC;
        const gpio_num_t enable_pin = arguments.size() > 2 ? (gpio_num_t)arguments[2]->evaluate_integer() : GPIO_NUM_NC;
        return std::make_shared<Expander>(name, serial, boot_pin, enable_pin, message_handler);
    } else if (type == "Bluetooth") {
        Module::expect(arguments, 1, string);
        std::string device_name = arguments[0]->evaluate_string();
        Bluetooth_ptr bluetooth = std::make_shared<Bluetooth>(name, device_name, message_handler);
        return bluetooth;
    } else if (type == "Output") {
        if (arguments.size() == 1) {
            Module::expect(arguments, 1, integer);
            return std::make_shared<GpioOutput>(name, (gpio_num_t)arguments[0]->evaluate_integer());
        } else {
            Module::expect(arguments, 2, identifier, integer);
            std::string mcp_name = arguments[0]->evaluate_identifier();
            Module_ptr module = Global::get_module(mcp_name);
            if (module->type != mcp23017) {
                throw std::runtime_error("module \"" + mcp_name + "\" is no mcp23017 port expander");
            }
            const Mcp23017_ptr mcp = std::static_pointer_cast<Mcp23017>(module);
            return std::make_shared<McpOutput>(name, mcp, arguments[1]->evaluate_integer());
        }
    } else if (type == "Input") {
        if (arguments.size() == 1) {
            Module::expect(arguments, 1, integer);
            return std::make_shared<GpioInput>(name, (gpio_num_t)arguments[0]->evaluate_integer());
        } else {
            Module::expect(arguments, 2, identifier, integer);
            std::string mcp_name = arguments[0]->evaluate_identifier();
            Module_ptr module = Global::get_module(mcp_name);
            if (module->type != mcp23017) {
                throw std::runtime_error("module \"" + mcp_name + "\" is no mcp23017 port expander");
            }
            const Mcp23017_ptr mcp = std::static_pointer_cast<Mcp23017>(module);
            return std::make_shared<McpInput>(name, mcp, arguments[1]->evaluate_integer());
        }
    } else if (type == "PwmOutput") {
        if (arguments.size() < 1 || arguments.size() > 3) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, integer, integer, integer);
        gpio_num_t pin = (gpio_num_t)arguments[0]->evaluate_integer();
        ledc_timer_t ledc_timer = arguments.size() > 1 ? (ledc_timer_t)arguments[1]->evaluate_integer() : LEDC_TIMER_0;
        ledc_channel_t ledc_channel = arguments.size() > 2 ? (ledc_channel_t)arguments[2]->evaluate_integer() : LEDC_CHANNEL_0;
        return std::make_shared<PwmOutput>(name, pin, ledc_timer, ledc_channel);
    } else if (type == "Mcp23017") {
        if (arguments.size() > 5) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, integer, integer, integer, integer, integer);
        i2c_port_t port = arguments.size() > 0 ? (i2c_port_t)arguments[0]->evaluate_integer() : I2C_NUM_0;
        gpio_num_t sda_pin = arguments.size() > 1 ? (gpio_num_t)arguments[1]->evaluate_integer() : DEFAULT_SDA_PIN;
        gpio_num_t scl_pin = arguments.size() > 2 ? (gpio_num_t)arguments[2]->evaluate_integer() : DEFAULT_SCL_PIN;
        uint8_t address = arguments.size() > 3 ? arguments[3]->evaluate_integer() : 0x20;
        int clk_speed = arguments.size() > 4 ? arguments[4]->evaluate_integer() : 100000;
        return std::make_shared<Mcp23017>(name, port, sda_pin, scl_pin, address, clk_speed);
    } else if (type == "Imu") {
        if (arguments.size() > 5) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, integer, integer, integer, integer, integer);
        i2c_port_t port = arguments.size() > 0 ? (i2c_port_t)arguments[0]->evaluate_integer() : I2C_NUM_0;
        gpio_num_t sda_pin = arguments.size() > 1 ? (gpio_num_t)arguments[1]->evaluate_integer() : DEFAULT_SDA_PIN;
        gpio_num_t scl_pin = arguments.size() > 2 ? (gpio_num_t)arguments[2]->evaluate_integer() : DEFAULT_SCL_PIN;
        uint8_t address = arguments.size() > 3 ? arguments[3]->evaluate_integer() : 0x28;
        int clk_speed = arguments.size() > 4 ? arguments[4]->evaluate_integer() : 100000;
        return std::make_shared<Imu>(name, port, sda_pin, scl_pin, address, clk_speed);
    } else if (type == "Can") {
        Module::expect(arguments, 3, integer, integer, integer, integer);
        gpio_num_t rx_pin = (gpio_num_t)arguments[0]->evaluate_integer();
        gpio_num_t tx_pin = (gpio_num_t)arguments[1]->evaluate_integer();
        long baud_rate = arguments[2]->evaluate_integer();
        return std::make_shared<Can>(name, rx_pin, tx_pin, baud_rate);
    } else if (type == "LinearMotor") {
        if (arguments.size() == 4) {
            Module::expect(arguments, 4, integer, integer, integer, integer);
            gpio_num_t move_in = (gpio_num_t)arguments[0]->evaluate_integer();
            gpio_num_t move_out = (gpio_num_t)arguments[1]->evaluate_integer();
            gpio_num_t end_in = (gpio_num_t)arguments[2]->evaluate_integer();
            gpio_num_t end_out = (gpio_num_t)arguments[3]->evaluate_integer();
            return std::make_shared<GpioLinearMotor>(name, move_in, move_out, end_in, end_out);
        } else {
            Module::expect(arguments, 5, identifier, integer, integer, integer, integer);
            std::string mcp_name = arguments[0]->evaluate_identifier();
            Module_ptr module = Global::get_module(mcp_name);
            if (module->type != mcp23017) {
                throw std::runtime_error("module \"" + mcp_name + "\" is no mcp23017 port expander");
            }
            const Mcp23017_ptr mcp = std::static_pointer_cast<Mcp23017>(module);
            uint8_t move_in = (gpio_num_t)arguments[1]->evaluate_integer();
            uint8_t move_out = (gpio_num_t)arguments[2]->evaluate_integer();
            uint8_t end_in = (gpio_num_t)arguments[3]->evaluate_integer();
            uint8_t end_out = (gpio_num_t)arguments[4]->evaluate_integer();
            return std::make_shared<McpLinearMotor>(name, mcp, move_in, move_out, end_in, end_out);
        }
    } else if (type == "ODriveMotor") {
        if (arguments.size() < 2 || arguments.size() > 3) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, identifier, integer, integer);
        std::string can_name = arguments[0]->evaluate_identifier();
        Module_ptr module = Global::get_module(can_name);
        if (module->type != can) {
            throw std::runtime_error("module \"" + can_name + "\" is no can connection");
        }
        const Can_ptr can = std::static_pointer_cast<Can>(module);
        uint32_t can_id = arguments[1]->evaluate_integer();
        int version = arguments.size() > 2 ? arguments[2]->evaluate_integer() : 4;
        ODriveMotor_ptr odrive_motor = std::make_shared<ODriveMotor>(name, can, can_id, version);
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
        Module::expect(arguments, 3, identifier, integer, integer);
        std::string can_name = arguments[0]->evaluate_identifier();
        Module_ptr module = Global::get_module(can_name);
        if (module->type != can) {
            throw std::runtime_error("module \"" + can_name + "\" is no can connection");
        }
        const Can_ptr can = std::static_pointer_cast<Can>(module);
        uint8_t motor_id = arguments[1]->evaluate_integer();
        int ratio = arguments[2]->evaluate_integer();
        RmdMotor_ptr rmd_motor = std::make_shared<RmdMotor>(name, can, motor_id, ratio);
        rmd_motor->subscribe_to_can();
        return rmd_motor;
    } else if (type == "RmdPair") {
        Module::expect(arguments, 2, identifier, identifier);
        std::string rmd1_name = arguments[0]->evaluate_identifier();
        Module_ptr module1 = Global::get_module(rmd1_name);
        if (module1->type != rmd_motor) {
            throw std::runtime_error("module \"" + rmd1_name + "\" is no RMD motor");
        }
        const RmdMotor_ptr rmd1 = std::static_pointer_cast<RmdMotor>(module1);
        std::string rmd2_name = arguments[1]->evaluate_identifier();
        Module_ptr module2 = Global::get_module(rmd2_name);
        if (module2->type != rmd_motor) {
            throw std::runtime_error("module \"" + rmd2_name + "\" is no RMD motor");
        }
        const RmdMotor_ptr rmd2 = std::static_pointer_cast<RmdMotor>(module2);
        return std::make_shared<RmdPair>(name, rmd1, rmd2);
    } else if (type == "Serial") {
        Module::expect(arguments, 4, integer, integer, integer, integer);
        gpio_num_t rx_pin = (gpio_num_t)arguments[0]->evaluate_integer();
        gpio_num_t tx_pin = (gpio_num_t)arguments[1]->evaluate_integer();
        long baud_rate = arguments[2]->evaluate_integer();
        uart_port_t uart_num = (uart_port_t)arguments[3]->evaluate_integer();
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
    } else if (type == "RoboClawWheels") {
        Module::expect(arguments, 2, identifier, identifier);
        const RoboClawMotor_ptr left_motor = get_module_paramter<RoboClawMotor>(arguments[0], roboclaw_motor, "roboclaw motor");
        const RoboClawMotor_ptr right_motor = get_module_paramter<RoboClawMotor>(arguments[1], roboclaw_motor, "roboclaw motor");
        return std::make_shared<RoboClawWheels>(name, left_motor, right_motor);
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
    } else if (type == "MotorAxis") {
        Module::expect(arguments, 3, identifier, identifier, identifier);
        const std::string name = arguments[0]->evaluate_identifier();
        Module_ptr module = Global::get_module(name);
        Motor_ptr motor;
        // TODO: rmd_motor, roboclaw_motor
        if (module->type == odrive_motor) {
            motor = get_module_paramter<ODriveMotor>(arguments[0], odrive_motor, "odrive_motor");
        } else if (module->type == stepper_motor) {
            motor = get_module_paramter<StepperMotor>(arguments[0], stepper_motor, "stepper_motor");
        } else if (module->type == canopen_motor) {
            motor = get_module_paramter<CanOpenMotor>(arguments[0], canopen_motor, "canopen_motor");
        } else {
            throw std::runtime_error("module \"" + name + "\" is not a supported motor for MotorAxis");
        }
        const Input_ptr input1 = get_module_paramter<Input>(arguments[1], input, "input");
        const Input_ptr input2 = get_module_paramter<Input>(arguments[2], input, "input");
        return std::make_shared<MotorAxis>(name, motor, input1, input2);
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
    } else if (type == "D1Motor") {
        Module::expect(arguments, 2, identifier, integer);
        const Can_ptr can_module = get_module_paramter<Can>(arguments[0], can, "can connection");
        const int64_t node_id = arguments[1]->evaluate_integer();
        D1Motor_ptr motor = std::make_shared<D1Motor>(name, can_module, node_id);
        motor->subscribe_to_can();
        return motor;
    } else if (type == "DunkerMotor") {
        Module::expect(arguments, 2, identifier, integer);
        const Can_ptr can_module = get_module_paramter<Can>(arguments[0], can, "can connection");
        const int64_t node_id = arguments[1]->evaluate_integer();
        DunkerMotor_ptr motor = std::make_shared<DunkerMotor>(name, can_module, node_id);
        motor->subscribe_to_can();
        return motor;
    } else if (type == "DunkerWheels") {
        Module::expect(arguments, 2, identifier, identifier);
        std::string left_name = arguments[0]->evaluate_identifier();
        std::string right_name = arguments[1]->evaluate_identifier();
        Module_ptr left_module = Global::get_module(left_name);
        Module_ptr right_module = Global::get_module(right_name);
        if (left_module->type != dunker_motor) {
            throw std::runtime_error("module \"" + left_name + "\" is no Dunker motor");
        }
        if (right_module->type != dunker_motor) {
            throw std::runtime_error("module \"" + right_name + "\" is no Dunker motor");
        }
        const DunkerMotor_ptr left_motor = std::static_pointer_cast<DunkerMotor>(left_module);
        const DunkerMotor_ptr right_motor = std::static_pointer_cast<DunkerMotor>(right_module);
        return std::make_shared<DunkerWheels>(name, left_motor, right_motor);
    } else if (type == "Analog") {
        // Usage: Analog <name> <AnalogUnit> <channel> [attenuation]
        if (arguments.size() < 2 || arguments.size() > 3) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, identifier, integer, numbery);
        const AnalogUnit_ptr unit_ref = get_module_paramter<AnalogUnit>(arguments[0], analog_unit, "analog unit");
        uint8_t channel = arguments[1]->evaluate_integer();
        float attenuation = arguments.size() > 2 ? arguments[2]->evaluate_number() : 11;
        Analog_ptr analog = std::make_shared<Analog>(name, unit_ref, channel, attenuation);
        return analog;
    } else if (type == "AnalogDual") {
        // Usage: AnalogDual <name> <AnalogUnit> <ch1> <ch2> [attenuation]
        if (arguments.size() < 3 || arguments.size() > 4) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, identifier, integer, integer, numbery);
        const AnalogUnit_ptr unit_ref = get_module_paramter<AnalogUnit>(arguments[0], analog_unit, "analog unit");
        uint8_t channel1 = arguments[1]->evaluate_integer();
        uint8_t channel2 = arguments[2]->evaluate_integer();
        float attenuation = arguments.size() > 3 ? arguments[3]->evaluate_number() : 11;
        AnalogDual_ptr analog_dual = std::make_shared<AnalogDual>(name, unit_ref, channel1, channel2, attenuation);
        return analog_dual;
    } else if (type == "AnalogUnit") {
        // Usage: AnalogUnit <name> <unit>
        Module::expect(arguments, 1, integer);
        uint8_t unit = arguments[0]->evaluate_integer();
        AnalogUnit_ptr au = std::make_shared<AnalogUnit>(name, unit);
        return au;
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
        int pos = csprintf(buffer, sizeof(buffer), "!!");
        for (auto const &[property_name, property] : this->properties) {
            pos += csprintf(&buffer[pos], sizeof(buffer) - pos, "%s.%s=", this->name.c_str(), property_name.c_str());
            pos += property->print_to_buffer(&buffer[pos], sizeof(buffer) - pos);
            pos += csprintf(&buffer[pos], sizeof(buffer) - pos, ";");
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

void Module::write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) {
    this->get_property(property_name)->assign(expression);
}

void Module::handle_can_msg(const uint32_t id, const int count, const uint8_t *data) {
    throw std::runtime_error("CAN message handler is not implemented");
}

DefaultsRegistry &Module::get_defaults_registry() {
    static DefaultsRegistry defaults_registry;
    return defaults_registry;
}

void Module::register_defaults(const std::string &type_name, DefaultsFunction defaults_function) {
    get_defaults_registry()[type_name] = defaults_function;
}

const std::map<std::string, Variable_ptr> Module::get_module_defaults(const std::string &type_name) {
    auto it = get_defaults_registry().find(type_name);
    if (it == get_defaults_registry().end()) {
        throw std::runtime_error("No defaults registered for module type \"" + type_name + "\"");
    }
    return it->second();
}
