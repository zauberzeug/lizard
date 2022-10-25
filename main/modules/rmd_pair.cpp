#include "rmd_pair.h"
#include <math.h>

RmdPair::RmdPair(const std::string name, const RmdMotor_ptr rmd1, const RmdMotor_ptr rmd2)
    : Module(rmd_pair, name), rmd1(rmd1), rmd2(rmd2) {
}

RmdPair::Trajectory RmdPair::compute_trajectory(double x0, double x1, double v0, double v1, double v_max, double a_max) {
    assert(v_max > 0);        // Positive velocity limit expected.
    assert(a_max > 0);        // Positive acceleration limit expected.
    assert(abs(v0) <= v_max); // Start velocity exceeds velocity limit.
    assert(abs(v1) <= v_max); // Target velocity exceeds velocity limit.

    Trajectory t;
    t.x0 = x0;
    t.v0 = v0;
    t.x1 = x1;
    t.v1 = v1;
    t.v_max = v_max;
    t.a_max = a_max;

    // find maximum possible velocity
    t.a = a_max;
    double r = (v0 * v0 + v1 * v1) / 2 + t.a * (x1 - x0);
    if (r < 0) {
        t.a = -a_max;
        r = (v0 * v0 + v1 * v1) / 2 + t.a * (x1 - x0);
    }
    t.t_acc = std::max((-v0 - std::sqrt(r)) / t.a, (-v0 + std::sqrt(r)) / t.a);
    double v_mid = v0 + t.t_acc * t.a;
    if (abs(v_mid) <= v_max) {
        // no linear part necessary
        t.t_lin = 0;
        t.t_dec = (v0 - v1) / t.a + t.t_acc;
        t.duration = t.t_acc + t.t_dec;
    } else {
        // insert linear part
        t.t_acc = abs(v_mid > 0 ? v_max - v0 : -v_max - v0) / a_max;
        t.t_dec = abs(v_mid > 0 ? v_max - v1 : -v_max - v1) / a_max;
        t.xa = x0 + v0 * t.t_acc + 1 / 2 * t.a * t.t_acc * t.t_acc;
        t.va = v0 + t.t_acc * t.a;
        double xb = x1 - v1 * t.t_dec - 1 / 2 * t.a * t.t_dec * t.t_dec;
        t.t_lin = abs(xb - t.xa) / abs(v_max);
        t.duration = t.t_acc + t.t_lin + t.t_dec;
    }

    return t;
}

void RmdPair::compute_trajectories(double x0, double y0, double x1, double y1, double v0, double w0, double v1, double w1,
                                   double v_max, double a_max, bool curved) {
    if (curved) {
        this->tx = this->compute_trajectory(x0, x1, v0, v1, v_max, a_max);
        this->ty = this->compute_trajectory(y0, y1, w0, w1, v_max, a_max);
    } else {
        double l = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
        this->t = this->compute_trajectory(0, v0, l, v1, v_max, a_max);
    }
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
