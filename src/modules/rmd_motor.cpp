#include "rmd_motor.h"

#include <cstring>

RmdMotor::RmdMotor(std::string name, Can *can) : Module(rmd_motor, name)
{
    this->can = can;
    this->properties["angle"] = new NumberVariable();
    this->properties["temperature"] = new IntegerVariable();
    this->properties["voltage"] = new NumberVariable();
    this->properties["error"] = new IntegerVariable();
    can->subscribe(this->can_id, this);
}

void RmdMotor::step()
{
    this->can->send(this->can_id, 0x92, 0, 0, 0, 0, 0, 0, 0);
    this->can->send(this->can_id, 0x9a, 0, 0, 0, 0, 0, 0, 0);
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
        int32_t speed = arguments[0]->evaluate_number() * 100;
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
            int32_t angle = arguments[0]->evaluate_number() * 100;
            this->can->send(this->can_id, 0xA3, 0,
                            0,
                            0,
                            *((uint8_t *)(&angle) + 0),
                            *((uint8_t *)(&angle) + 1),
                            *((uint8_t *)(&angle) + 2),
                            *((uint8_t *)(&angle) + 3));
        }
        else
        {
            Module::expect(arguments, 2, numbery, numbery);
            int32_t angle = arguments[0]->evaluate_number() * 100;
            uint16_t speed = arguments[1]->evaluate_number();
            this->can->send(this->can_id, 0xA4, 0,
                            *((uint8_t *)(&speed) + 0),
                            *((uint8_t *)(&speed) + 1),
                            *((uint8_t *)(&angle) + 0),
                            *((uint8_t *)(&angle) + 1),
                            *((uint8_t *)(&angle) + 2),
                            *((uint8_t *)(&angle) + 3));
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
        int32_t angle = this->properties["angle"]->number_value * 100;
        this->can->send(this->can_id, 0xA3, 0,
                        0,
                        0,
                        *((uint8_t *)(&angle) + 0),
                        *((uint8_t *)(&angle) + 1),
                        *((uint8_t *)(&angle) + 2),
                        *((uint8_t *)(&angle) + 3));
    }
    else if (method_name == "tune")
    {
        Module::expect(arguments, 0); // TODO
        throw std::runtime_error("tune command is not implemented yet");
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
    if (data[0] == 0x92)
    {
        int64_t value = 0;
        std::memcpy(&value, data + 1, 7);
        this->properties["angle"]->number_value = (value << 8) / 256.0 / 100.0;
    }
    if (data[0] == 0x9a)
    {
        int8_t temperature = 0;
        std::memcpy(&temperature, data + 1, 1);
        this->properties["temperature"]->integer_value = temperature;
        uint16_t voltage = 0;
        std::memcpy(&voltage, data + 3, 2);
        this->properties["voltage"]->number_value = 0.1 * voltage;
        this->properties["error"]->integer_value = data[7];
    }
}
