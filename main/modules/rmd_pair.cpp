#include "rmd_pair.h"
#include <math.h>

RmdPair::RmdPair(const std::string name, const RmdMotor_ptr rmd1, const RmdMotor_ptr rmd2)
    : Module(rmd_pair, name), rmd1(rmd1), rmd2(rmd2) {
}

void RmdPair::compute_trajectory(int index, double x0, double x1, double v0, double v1, double v_max, double a_max) {
    assert(v_max > 0);        // Positive velocity limit expected.
    assert(a_max > 0);        // Positive acceleration limit expected.
    assert(abs(v0) <= v_max); // Start velocity exceeds velocity limit.
    assert(abs(v1) <= v_max); // Target velocity exceeds velocity limit.

    TrajectoryPart ta = index == 1 ? this->t1a : this->t2a;
    TrajectoryPart tb = index == 1 ? this->t1b : this->t2b;
    TrajectoryPart tc = index == 1 ? this->t1c : this->t2c;

    // find maximum possible velocity
    double a = a_max;
    double r = (v0 * v0 + v1 * v1) / 2 + a * (x1 - x0);
    if (r < 0) {
        a = -a_max;
        r = (v0 * v0 + v1 * v1) / 2 + a * (x1 - x0);
    }
    double dt_acc = std::max((-v0 - std::sqrt(r)) / a, (-v0 + std::sqrt(r)) / a);
    double dt_dec = (v0 - v1) / a + dt_acc;
    double v_mid = v0 + dt_acc * a;
    if (abs(v_mid) <= v_max) {
        // no linear part necessary
        double x_mid = x0 + v0 * dt_acc + 1 / 2 * a * dt_acc * dt_acc;
        ta = (TrajectoryPart){.t0 = 0, .x0 = x0, .v0 = v0, .a = a, .dt = dt_acc};
        tb = (TrajectoryPart){.t0 = dt_acc, .x0 = x_mid, .v0 = v_mid, .a = 0, .dt = 0};
        tc = (TrajectoryPart){.t0 = dt_acc, .x0 = x_mid, .v0 = v_mid, .a = -a, .dt = dt_dec};
    } else {
        // insert linear part
        dt_acc = abs(v_mid > 0 ? v_max - v0 : -v_max - v0) / a_max;
        dt_dec = abs(v_mid > 0 ? v_max - v1 : -v_max - v1) / a_max;
        double xa = x0 + v0 * dt_acc + 1 / 2 * a * dt_acc * dt_acc;
        double xb = x1 - v1 * dt_dec - 1 / 2 * a * dt_dec * dt_dec;
        double v_lin = v0 + dt_acc * a;
        double dt_lin = abs(xb - xa) / abs(v_max);
        ta = (TrajectoryPart){.t0 = 0, .x0 = x0, .v0 = v0, .a = a, .dt = dt_acc};
        tb = (TrajectoryPart){.t0 = dt_acc, .x0 = xa, .v0 = v_lin, .a = 0, .dt = dt_lin};
        tc = (TrajectoryPart){.t0 = dt_acc + dt_lin, .x0 = xb, .v0 = v_lin, .a = -a, .dt = dt_dec};
    }
}

void RmdPair::throttle(TrajectoryPart &part, double factor) {
    part.t0 *= factor;
    part.v0 /= factor;
    part.a /= factor * factor;
    part.dt *= factor;
}

void RmdPair::compute_trajectories(double x0, double y0, double x1, double y1, double v0, double w0, double v1, double w1,
                                   double v_max, double a_max, bool curved) {
    if (curved) {
        this->compute_trajectory(1, x0, x1, v0, v1, v_max, a_max);
        this->compute_trajectory(2, y0, y1, w0, w1, v_max, a_max);
    } else {
        double yaw = std::atan2(y1 - y0, x1 - x0);
        this->compute_trajectory(1, x0, x1, v0 * std::cos(yaw), v1 * std::cos(yaw), v_max * std::cos(yaw), a_max * std::cos(yaw));
        this->compute_trajectory(2, y0, y1, v0 * std::sin(yaw), v1 * std::sin(yaw), v_max * std::sin(yaw), a_max * std::sin(yaw));
    }
    double duration1 = this->t1a.dt + this->t1b.dt + this->t1c.dt;
    double duration2 = this->t2a.dt + this->t2b.dt + this->t2c.dt;
    double duration = std::max(duration1, duration2);
    throttle(this->t1a, duration / duration1);
    throttle(this->t1b, duration / duration1);
    throttle(this->t1c, duration / duration1);
    throttle(this->t2a, duration / duration2);
    throttle(this->t2b, duration / duration2);
    throttle(this->t2c, duration / duration2);
}

void RmdPair::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "move") {
        if (arguments.size() == 8) {
            Module::expect(arguments, 8, numbery, numbery, numbery, numbery, numbery, numbery, numbery, numbery);
            this->compute_trajectories(
                arguments[0]->evaluate_number(),
                arguments[1]->evaluate_number(),
                arguments[2]->evaluate_number(),
                arguments[3]->evaluate_number(),
                arguments[4]->evaluate_number(),
                arguments[4]->evaluate_number(),
                arguments[5]->evaluate_number(),
                arguments[5]->evaluate_number(),
                arguments[6]->evaluate_number(),
                arguments[7]->evaluate_number(),
                false);
        } else {
            Module::expect(arguments, 10, numbery, numbery, numbery, numbery, numbery, numbery, numbery, numbery, numbery, numbery);
            this->compute_trajectories(
                arguments[0]->evaluate_number(),
                arguments[1]->evaluate_number(),
                arguments[2]->evaluate_number(),
                arguments[3]->evaluate_number(),
                arguments[4]->evaluate_number(),
                arguments[5]->evaluate_number(),
                arguments[6]->evaluate_number(),
                arguments[7]->evaluate_number(),
                arguments[8]->evaluate_number(),
                arguments[9]->evaluate_number(),
                false);
        }
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->rmd1->stop();
        this->rmd2->stop();
    } else if (method_name == "resume") {
        Module::expect(arguments, 0);
        this->rmd1->resume();
        this->rmd2->resume();
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->rmd1->off();
        this->rmd2->off();
    } else if (method_name == "hold") {
        Module::expect(arguments, 0);
        this->rmd1->hold();
        this->rmd2->hold();
    } else if (method_name == "clear_errors") {
        Module::expect(arguments, 0);
        this->rmd1->clear_errors();
        this->rmd2->clear_errors();
    } else {
        Module::call(method_name, arguments);
    }
}
