#pragma once

#include "module.h"
#include "rmd_motor.h"

class RmdPair : public Module {
private:
    const RmdMotor_ptr rmd1;
    const RmdMotor_ptr rmd2;

public:
    RmdPair(const std::string name, const RmdMotor_ptr rmd1, const RmdMotor_ptr rmd2);
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
};
