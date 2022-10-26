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

    double t_end(TrajectoryPart part);
    double x_end(TrajectoryPart part);
    double x(TrajectoryPart part, double t);
    void throttle(TrajectoryPart &part, double factor);
    TrajectoryTriple compute_trajectory(double x0, double x1, double v0, double v1, double v_max, double a_max);
    void schedule_trajectories(double x0, double y0, double x1, double y1, double v0, double w0, double v1, double w1,
                               double v_max, double a_max, bool curved);

public:
    RmdPair(const std::string name, const RmdMotor_ptr rmd1, const RmdMotor_ptr rmd2);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
};
