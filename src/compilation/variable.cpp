#include "variable.h"

void Variable::set_number(double value)
{
    this->type = number;
    this->number_value = value;
}