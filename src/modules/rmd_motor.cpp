#include "rmd_motor.h"

#include <cstring>

RmdMotor::RmdMotor(std::string name, Can *can) : Module(rmd_motor, name)
{
    this->can = can;
    this->properties["position"] = new NumberVariable();
    this->properties["ratio"] = new NumberVariable(6.0);
    can->subscribe(this->can_id, this);
}

void RmdMotor::step()
{
    this->can->send(this->can_id, 0x92, 0, 0, 0, 0, 0, 0, 0);
    Module::step();
}

void RmdMotor::call(std::string method_name, std::vector<Expression *> arguments)
{
    if (method_name == "zero")
    {
        Module::expect(arguments, 0);
        this->can->send(this->can_id, 0x19, 0, 0, 0, 0, 0, 0, 0);
    }
    else if (method_name == "power")
    {
        Module::expect(arguments, 1, numbery);
        int16_t power = arguments[0]->evaluate_number() * 2000;
        this->can->send(this->can_id, 0xA1, 0,
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
        int32_t speed = arguments[0]->evaluate_number() * 100 * this->properties["ratio"]->number_value;
        this->can->send(this->can_id, 0xA2, 0,
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
            int32_t position = arguments[0]->evaluate_number() * 100 * this->properties["ratio"]->number_value;
            this->can->send(this->can_id, 0xA3, 0,
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
            int32_t position = arguments[0]->evaluate_number() * 100 * this->properties["ratio"]->number_value;
            uint16_t speed = arguments[1]->evaluate_number() * 100 * this->properties["ratio"]->number_value;
            this->can->send(this->can_id, 0xA4, 0,
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
        this->can->send(this->can_id, 0x81, 0, 0, 0, 0, 0, 0, 0);
    }
    else if (method_name == "resume")
    {
        Module::expect(arguments, 0);
        this->can->send(this->can_id, 0x88, 0, 0, 0, 0, 0, 0, 0);
    }
    else if (method_name == "off")
    {
        Module::expect(arguments, 0);
        this->can->send(this->can_id, 0x80, 0, 0, 0, 0, 0, 0, 0);
    }
    else if (method_name == "hold")
    {
        Module::expect(arguments, 0);
        int32_t position = this->properties["position"]->number_value * 100;
        this->can->send(this->can_id, 0xA3, 0,
                        0,
                        0,
                        *((uint8_t *)(&position) + 0),
                        *((uint8_t *)(&position) + 1),
                        *((uint8_t *)(&position) + 2),
                        *((uint8_t *)(&position) + 3));
    }
    else if (method_name == "get_health")
    {
        Module::expect(arguments, 0);
        this->can->send(this->can_id, 0x9a, 0, 0, 0, 0, 0, 0, 0);
    }
    else if (method_name == "get_pid")
    {
        Module::expect(arguments, 0);
        this->can->send(this->can_id, 0x30, 0, 0, 0, 0, 0, 0, 0);
    }
    else if (method_name == "set_pid")
    {
        Module::expect(arguments, 6, integer, integer, integer, integer, integer, integer);
        this->can->send(this->can_id, 0x32, 0,
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
        this->can->send(this->can_id, 0x33, 0, 0, 0, 0, 0, 0, 0);
    }
    else if (method_name == "set_acceleration")
    {
        Module::expect(arguments, 1, numbery);
        int32_t acceleration = arguments[0]->evaluate_number();
        this->can->send(this->can_id, 0x34, 0,
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
        this->can->send(this->can_id, 0x9b, 0, 0, 0, 0, 0, 0, 0);
    }
    else
    {
        Module::call(method_name, arguments);
    }
}

void RmdMotor::handle_can_msg(uint32_t id, int count, uint8_t *data)
{
    switch (data[0])
    {
    case 0x92:
    {
        int64_t value = 0;
        std::memcpy(&value, data + 1, 7);
        this->properties["position"]->number_value = (value << 8) / 256.0 / 100.0 / this->properties["ratio"]->number_value;
        break;
    }
    case 0x9a:
    {
        int8_t temperature = 0;
        std::memcpy(&temperature, data + 1, 1);
        uint16_t voltage = 0;
        std::memcpy(&voltage, data + 3, 2);
        uint8_t error = data[7];
        printf("%s health %d %.1f %d\n", this->name.c_str(), temperature, 0.1 * voltage, error);
        break;
    }
    case 0x30:
    {
        printf("%s pid %3d %3d %3d %3d %3d %3d\n",
               this->name.c_str(), data[2], data[3], data[4], data[5], data[6], data[7]);
        break;
    }
    case 0x33:
    {
        int32_t acceleration = 0;
        std::memcpy(&acceleration, data + 4, 4);
        printf("%s acceleration %d\n", this->name.c_str(), acceleration);
        break;
    }
    }
}