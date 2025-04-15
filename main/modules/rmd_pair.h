#pragma once

#include "module.h"
#include "rmd_motor.h"

class RmdPair : public Module {
private:
    const RmdMotor_ptr rmd1;
    const RmdMotor_ptr rmd2;
    bool enabled = true;

    struct TrajectoryPart {
        double t0;
        double x0;
        double v0;
        double a;
        double dt;
    };

    struct TrajectoryTriple {
        TrajectoryPart part_a;
        TrajectoryPart part_b;
        TrajectoryPart part_c;
    };

    void throttle(TrajectoryPart &part, double factor) const;
    TrajectoryTriple compute_trajectory(double x0, double x1, double v0, double v1) const;
    void move(double x, double y);
    void enable();
    void disable();

public:
    RmdPair(const std::string name, const RmdMotor_ptr rmd1, const RmdMotor_ptr rmd2);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
