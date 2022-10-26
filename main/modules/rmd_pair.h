#pragma once

#include "module.h"
#include "rmd_motor.h"
#include <list>

class RmdPair : public Module {
private:
    const RmdMotor_ptr rmd1;
    const RmdMotor_ptr rmd2;

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

    std::list<TrajectoryPart> schedule1;
    std::list<TrajectoryPart> schedule2;

    double t_end(TrajectoryPart part) const;
    double x_end(TrajectoryPart part) const;
    double v_end(TrajectoryPart part) const;
    double x(TrajectoryPart part, double t) const;
    double v(TrajectoryPart part, double t) const;
    void throttle(TrajectoryPart &part, double factor) const;
    TrajectoryTriple compute_trajectory(double x0, double x1, double v0, double v1) const;
    void schedule_trajectories(double x, double y, double v, double w);

public:
    RmdPair(const std::string name, const RmdMotor_ptr rmd1, const RmdMotor_ptr rmd2);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
};
