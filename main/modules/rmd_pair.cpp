#include "rmd_pair.h"
#include "utils/timing.h"
#include <math.h>

RmdPair::RmdPair(const std::string name, const RmdMotor_ptr rmd1, const RmdMotor_ptr rmd2)
    : Module(rmd_pair, name), rmd1(rmd1), rmd2(rmd2) {
}

RmdPair::TrajectoryTriple RmdPair::compute_trajectory(double x0, double x1, double v0, double v1, double v_max, double a_max) {
    assert(v_max > 0);        // Positive velocity limit expected.
    assert(a_max > 0);        // Positive acceleration limit expected.
    assert(abs(v0) <= v_max); // Start velocity exceeds velocity limit.
    assert(abs(v1) <= v_max); // Target velocity exceeds velocity limit.

    TrajectoryTriple result;

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
        double x_mid = x0 + v0 * dt_acc + 0.5 * a * dt_acc * dt_acc;
        result.part_a = (TrajectoryPart){.t0 = 0, .x0 = x0, .v0 = v0, .a = a, .dt = dt_acc};
        result.part_b = (TrajectoryPart){.t0 = dt_acc, .x0 = x_mid, .v0 = v_mid, .a = 0, .dt = 0};
        result.part_c = (TrajectoryPart){.t0 = dt_acc, .x0 = x_mid, .v0 = v_mid, .a = -a, .dt = dt_dec};
    } else {
        // insert linear part
        dt_acc = abs(v_mid > 0 ? v_max - v0 : -v_max - v0) / a_max;
        dt_dec = abs(v_mid > 0 ? v_max - v1 : -v_max - v1) / a_max;
        double xa = x0 + v0 * dt_acc + 0.5 * a * dt_acc * dt_acc;
        double xb = x1 - v1 * dt_dec - 0.5 * a * dt_dec * dt_dec;
        double v_lin = v0 + dt_acc * a;
        double dt_lin = abs(xb - xa) / abs(v_max);
        result.part_a = (TrajectoryPart){.t0 = 0, .x0 = x0, .v0 = v0, .a = a, .dt = dt_acc};
        result.part_b = (TrajectoryPart){.t0 = dt_acc, .x0 = xa, .v0 = v_lin, .a = 0, .dt = dt_lin};
        result.part_c = (TrajectoryPart){.t0 = dt_acc + dt_lin, .x0 = xb, .v0 = v_lin, .a = -a, .dt = dt_dec};
    }

    return result;
}

double RmdPair::t_end(TrajectoryPart part) {
    return part.t0 + part.dt;
}

double RmdPair::x_end(TrajectoryPart part) {
    return this->x(part, this->t_end(part));
}

double RmdPair::x(TrajectoryPart part, double t) {
    double dt = t - part.t0;
    return part.x0 + part.v0 * dt + 0.5 * part.a * dt * dt;
}

void RmdPair::throttle(TrajectoryPart &part, double factor) {
    part.t0 *= factor;
    part.v0 /= factor;
    part.a /= factor * factor;
    part.dt *= factor;
}

void RmdPair::schedule_trajectories(double x0, double y0, double x1, double y1, double v0, double w0, double v1, double w1,
                                    double v_max, double a_max, bool curved) {
    TrajectoryTriple t1;
    TrajectoryTriple t2;
    if (curved) {
        t1 = this->compute_trajectory(x0, x1, v0, v1, v_max, a_max);
        t2 = this->compute_trajectory(y0, y1, w0, w1, v_max, a_max);
    } else {
        double yaw = std::atan2(y1 - y0, x1 - x0);
        t1 = this->compute_trajectory(x0, x1, v0 * std::cos(yaw), v1 * std::cos(yaw), v_max * std::cos(yaw), a_max * std::cos(yaw));
        t2 = this->compute_trajectory(y0, y1, v0 * std::sin(yaw), v1 * std::sin(yaw), v_max * std::sin(yaw), a_max * std::sin(yaw));
    }
    double duration1 = t1.part_a.dt + t1.part_b.dt + t1.part_c.dt;
    double duration2 = t2.part_a.dt + t2.part_b.dt + t2.part_c.dt;
    double duration = std::max(duration1, duration2);
    throttle(t1.part_a, duration / duration1);
    throttle(t1.part_b, duration / duration1);
    throttle(t1.part_c, duration / duration1);
    throttle(t2.part_a, duration / duration2);
    throttle(t2.part_b, duration / duration2);
    throttle(t2.part_c, duration / duration2);
    t1.part_a.t0 = this->schedule1.empty() ? millis() / 1000.0 : this->t_end(this->schedule1.back());
    t1.part_b.t0 = this->t_end(t1.part_a);
    t1.part_c.t0 = this->t_end(t1.part_b);
    t2.part_a.t0 = this->schedule2.empty() ? millis() / 1000.0 : this->t_end(this->schedule2.back());
    t2.part_b.t0 = this->t_end(t2.part_a);
    t2.part_c.t0 = this->t_end(t2.part_b);
    this->schedule1.push_back(t1.part_a);
    this->schedule1.push_back(t1.part_b);
    this->schedule1.push_back(t1.part_c);
    this->schedule2.push_back(t2.part_a);
    this->schedule2.push_back(t2.part_b);
    this->schedule2.push_back(t2.part_c);
}

void RmdPair::step() {
    double t = millis() / 1000.0;
    while (!this->schedule1.empty() && this->t_end(this->schedule1.front()) < t) {
        this->schedule1.pop_front();
    }
    while (!this->schedule2.empty() && this->t_end(this->schedule2.front()) < t) {
        this->schedule2.pop_front();
    }
    if (!this->schedule1.empty()) {
        double position = this->x(this->schedule1.front(), t);
        printf("%.1f\n", position);
    }
    if (!this->schedule2.empty()) {
        double position = this->x(this->schedule2.front(), t);
        printf("             %.1f\n", position);
    }
    Module::step();
}

void RmdPair::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "move") {
        if (arguments.size() == 8) {
            Module::expect(arguments, 8, numbery, numbery, numbery, numbery, numbery, numbery, numbery, numbery);
            this->schedule_trajectories(
                arguments[0]->evaluate_number(),
                arguments[1]->evaluate_number(),
                arguments[2]->evaluate_number(),
                arguments[3]->evaluate_number(),
                arguments[4]->evaluate_number(),
                0,
                arguments[5]->evaluate_number(),
                0,
                arguments[6]->evaluate_number(),
                arguments[7]->evaluate_number(),
                false);
        } else {
            Module::expect(arguments, 10, numbery, numbery, numbery, numbery, numbery, numbery, numbery, numbery, numbery, numbery);
            this->schedule_trajectories(
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
