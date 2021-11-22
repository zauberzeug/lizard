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
