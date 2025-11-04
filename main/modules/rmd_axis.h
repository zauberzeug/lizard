#pragma once

#include "input.h"
#include "rmd_8x_pro_v2.h"

class RmdAxis : public Module {
private:
    const Rmd8xProV2_ptr motor;
    const Input_ptr top_endstop;
    bool enabled = true;
    const bool inverted;

    bool can_move_positive(const float value) const;
    void enable();
    void disable();

public:
    RmdAxis(const std::string name, const Rmd8xProV2_ptr motor, const Input_ptr top_endstop, const bool inverted);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
