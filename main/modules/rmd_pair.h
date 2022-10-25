#pragma once

#include "module.h"
#include "rmd_motor.h"

class RmdPair : public Module {
private:
    const RmdMotor_ptr rmd1;
    const RmdMotor_ptr rmd2;
    struct Trajectory {
        double x0;
        double v0;
        double x1;
        double v1;
        double v_max;
        double a_max;
        double a;
        double t_acc;
        double t_lin;
        double t_dec;
        double xa;
        double va;
        double duration;
    } t, tx, ty;

    Trajectory compute_trajectory(double x0, double x1, double v0, double v1, double v_max, double a_max);
    void compute_trajectories(double x0, double y0, double x1, double y1, double v0, double w0, double v1, double w1,
                              double v_max, double a_max, bool curved);

public:
    RmdPair(const std::string name, const RmdMotor_ptr rmd1, const RmdMotor_ptr rmd2);
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
};
