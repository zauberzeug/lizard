#include "rmd_motor.h"

#include <cstring>
#include "../global.h"
#include "../utils/echo.h"
#include "../utils/timing.h"

RmdMotor::RmdMotor(const std::string name, Can *const can, const uint8_t motor_id)
    : Module(rmd_motor, name), can_id(0x140 + motor_id), can(can)
{
    this->properties["position"] = new NumberVariable();
    this->properties["ratio"] = new NumberVariable(6.0);
    this->properties["torque"] = new NumberVariable();
    this->properties["speed"] = new NumberVariable();
    can->subscribe(this->can_id, this);
}

void RmdMotor::send_and_wait(const uint32_t id,
                             const uint8_t d0, const uint8_t d1, const uint8_t d2, const uint8_t d3,
                             const uint8_t d4, const uint8_t d5, const uint8_t d6, const uint8_t d7,
                             const unsigned long int timeout_ms)
{
    this->last_msg = 0;
    this->can->send(id, d0, d1, d2, d3, d4, d5, d6, d7);
    unsigned long int start = micros();
    while (this->last_msg != d0 && micros_since(start) < timeout_ms * 1000)
    {
        this->can->step();
    }
}

void RmdMotor::step()
{
    if (this->last_step_time == 0)
    {
        this->last_step_time = micros();
        return;
    }
    double seconds_per_step = micros_since(this->last_step_time) / 1e6;
    this->last_step_time = micros();

    this->send_and_wait(this->can_id, 0x92, 0, 0, 0, 0, 0, 0, 0);
    this->send_and_wait(this->can_id, 0x9c, 0, 0, 0, 0, 0, 0, 0);
    if (this->leader)
    {
        double own_position = this->properties.at("position")->number_value;
        double leader_position = this->leader->properties.at("position")->number_value;
        double target_speed = (leader_position - own_position - leader_offset) / seconds_per_step;
        int32_t speed = target_speed * 100 * this->properties.at("ratio")->number_value;
        this->send_and_wait(this->can_id, 0xa2, 0,
                            0,
                            0,
                            *((uint8_t *)(&speed) + 0),
                            *((uint8_t *)(&speed) + 1),
                            *((uint8_t *)(&speed) + 2),
                            *((uint8_t *)(&speed) + 3));
    }
    Module::step();
}

void RmdMotor::call(const std::string method_name, const std::vector<const Expression *> arguments)
{
    if (method_name == "zero")
    {
        Module::expect(arguments, 0);
        this->send_and_wait(this->can_id, 0x19, 0, 0, 0, 0, 0, 0, 0, 250);
    }
    else if (method_name == "power")
    {
        Module::expect(arguments, 1, numbery);
        int16_t power = arguments[0]->evaluate_number() * 2000;
        this->send_and_wait(this->can_id, 0xa1, 0,
                            0,
                            0,
                            *((uint8_t *)(&power) + 0),
                            *((uint8_t *)(&power) + 1),
                            0,
                            0);
    }
    else if (method_name == "speed")
    {
        Module::expect(arguments, 1, numbery);
        int32_t speed = arguments[0]->evaluate_number() * 100 * this->properties.at("ratio")->number_value;
        this->send_and_wait(this->can_id, 0xa2, 0,
                            0,
                            0,
                            *((uint8_t *)(&speed) + 0),
                            *((uint8_t *)(&speed) + 1),
                            *((uint8_t *)(&speed) + 2),
                            *((uint8_t *)(&speed) + 3));
    }
    else if (method_name == "position")
    {
        if (arguments.size() == 1)
        {
            Module::expect(arguments, 1, numbery);
            int32_t position = arguments[0]->evaluate_number() * 100 * this->properties.at("ratio")->number_value;
            this->send_and_wait(this->can_id, 0xa3, 0,
                                0,
                                0,
                                *((uint8_t *)(&position) + 0),
                                *((uint8_t *)(&position) + 1),
                                *((uint8_t *)(&position) + 2),
                                *((uint8_t *)(&position) + 3));
        }
        else
        {
            Module::expect(arguments, 2, numbery, numbery);
            int32_t position = arguments[0]->evaluate_number() * 100 * this->properties.at("ratio")->number_value;
            uint16_t speed = arguments[1]->evaluate_number() * 100 * this->properties.at("ratio")->number_value;
            this->send_and_wait(this->can_id, 0xa4, 0,
                                *((uint8_t *)(&speed) + 0),
                                *((uint8_t *)(&speed) + 1),
                                *((uint8_t *)(&position) + 0),
                                *((uint8_t *)(&position) + 1),
                                *((uint8_t *)(&position) + 2),
                                *((uint8_t *)(&position) + 3));
        }
    }
    else if (method_name == "stop")
    {
        Module::expect(arguments, 0);
        this->send_and_wait(this->can_id, 0x81, 0, 0, 0, 0, 0, 0, 0);
    }
    else if (method_name == "resume")
    {
        Module::expect(arguments, 0);
        this->send_and_wait(this->can_id, 0x88, 0, 0, 0, 0, 0, 0, 0);
    }
    else if (method_name == "off")
    {
        Module::expect(arguments, 0);
        this->send_and_wait(this->can_id, 0x80, 0, 0, 0, 0, 0, 0, 0);
    }
    else if (method_name == "hold")
    {
        Module::expect(arguments, 0);
        int32_t position = this->properties.at("position")->number_value * 100;
        this->send_and_wait(this->can_id, 0xa3, 0,
                            0,
                            0,
                            *((uint8_t *)(&position) + 0),
                            *((uint8_t *)(&position) + 1),
                            *((uint8_t *)(&position) + 2),
                            *((uint8_t *)(&position) + 3));
    }
    else if (method_name == "follow")
    {
        Module::expect(arguments, 1, identifier);
        std::string other_name = arguments[0]->evaluate_identifier();
        const Module *const other = Global::get_module(other_name);
        if (other->type != rmd_motor)
        {
            throw std::runtime_error("module \"" + other_name + "\" is no RMD motor");
        }
        this->leader = (const RmdMotor *)other;
        this->leader_offset =
            this->leader->properties.at("position")->number_value -
            this->properties.at("position")->number_value;
    }
    else if (method_name == "unfollow")
    {
        this->leader = nullptr;
    }
    else if (method_name == "get_health")
    {
        Module::expect(arguments, 0);
        this->send_and_wait(this->can_id, 0x9a, 0, 0, 0, 0, 0, 0, 0);
    }
    else if (method_name == "get_pid")
    {
        Module::expect(arguments, 0);
        this->send_and_wait(this->can_id, 0x30, 0, 0, 0, 0, 0, 0, 0);
    }
    else if (method_name == "set_pid")
    {
        Module::expect(arguments, 6, integer, integer, integer, integer, integer, integer);
        this->send_and_wait(this->can_id, 0x32, 0,
                            arguments[0]->evaluate_integer(),
                            arguments[1]->evaluate_integer(),
                            arguments[2]->evaluate_integer(),
                            arguments[3]->evaluate_integer(),
                            arguments[4]->evaluate_integer(),
                            arguments[5]->evaluate_integer());
    }
    else if (method_name == "get_acceleration")
    {
        Module::expect(arguments, 0);
        this->send_and_wait(this->can_id, 0x33, 0, 0, 0, 0, 0, 0, 0);
    }
    else if (method_name == "set_acceleration")
    {
        Module::expect(arguments, 1, numbery);
        int32_t acceleration = arguments[0]->evaluate_number();
        this->send_and_wait(this->can_id, 0x34, 0,
                            0,
                            0,
                            *((uint8_t *)(&acceleration) + 0),
                            *((uint8_t *)(&acceleration) + 1),
                            *((uint8_t *)(&acceleration) + 2),
                            *((uint8_t *)(&acceleration) + 3));
    }
    else if (method_name == "clear_errors")
    {
        Module::expect(arguments, 0);
        this->send_and_wait(this->can_id, 0x9b, 0, 0, 0, 0, 0, 0, 0);
    }
    else
    {
        Module::call(method_name, arguments);
    }
}

void RmdMotor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data)
{
    switch (data[0])
    {
    case 0x92:
    {
        int64_t value = 0;
        std::memcpy(&value, data + 1, 7);
        this->properties.at("position")->number_value = (value << 8) / 256.0 / 100.0 / this->properties.at("ratio")->number_value;
        break;
    }
    case 0x9a:
    {
        int8_t temperature = 0;
        std::memcpy(&temperature, data + 1, 1);
        uint16_t voltage = 0;
        std::memcpy(&voltage, data + 3, 2);
        uint8_t error = data[7];
        echo(all, text, "%s health %d %.1f %d", this->name.c_str(), temperature, 0.1 * voltage, error);
        break;
    }
    case 0x9c:
    {
        int16_t torque = 0;
        std::memcpy(&torque, data + 2, 2);
        this->properties.at("torque")->number_value = torque / 2048.0 * 33.0;
        int16_t speed = 0;
        std::memcpy(&speed, data + 4, 2);
        this->properties.at("speed")->number_value = speed / this->properties.at("ratio")->number_value;
        break;
    }
    case 0x30:
    {
        echo(all, text, "%s pid %3d %3d %3d %3d %3d %3d",
             this->name.c_str(), data[2], data[3], data[4], data[5], data[6], data[7]);
        break;
    }
    case 0x33:
    {
        int32_t acceleration = 0;
        std::memcpy(&acceleration, data + 4, 4);
        echo(all, text, "%s acceleration %d", this->name.c_str(), acceleration);
        break;
    }
    }
    this->last_msg = data[0];
}