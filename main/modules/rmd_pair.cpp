#include "rmd_pair.h"
#include "utils/timing.h"
#include "utils/uart.h"
#include <math.h>

RmdPair::RmdPair(const std::string name, const RmdMotor_ptr rmd1, const RmdMotor_ptr rmd2)
    : Module(rmd_pair, name), rmd1(rmd1), rmd2(rmd2) {
    this->properties["v_max"] = std::make_shared<NumberVariable>(360);
    this->properties["a_max"] = std::make_shared<NumberVariable>(360);
    this->properties["max_error"] = std::make_shared<NumberVariable>(10);
    this->properties["dt"] = std::make_shared<NumberVariable>(0.02);
}

RmdPair::TrajectoryTriple RmdPair::compute_trajectory(double x0, double x1, double v0, double v1) const {
    const double v_max = std::abs(this->properties.at("v_max")->number_value);
    const double a_max = std::abs(this->properties.at("a_max")->number_value);
    v0 = std::min(std::max(v0, -v_max), v_max);
    v1 = std::min(std::max(v1, -v_max), v_max);

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

double RmdPair::t_end(TrajectoryPart part) const {
    return part.t0 + part.dt;
}

double RmdPair::x_end(TrajectoryPart part) const {
    return this->x(part, this->t_end(part));
}

double RmdPair::v_end(TrajectoryPart part) const {
    return this->v(part, this->t_end(part));
}

double RmdPair::x(TrajectoryPart part, double t) const {
    double dt = t - part.t0;
    return part.x0 + part.v0 * dt + 0.5 * part.a * dt * dt;
}

double RmdPair::v(TrajectoryPart part, double t) const {
    return part.v0 + part.a * (t - part.t0);
}

void RmdPair::throttle(TrajectoryPart &part, double factor) const {
    part.t0 *= factor;
    part.v0 /= factor;
    part.a /= factor * factor;
    part.dt *= factor;
}

void RmdPair::schedule_trajectories(double x, double y, double v, double w) {
    double x0 = this->schedule1.empty() ? rmd1->get_position() : this->x_end(this->schedule1.back());
    double y0 = this->schedule2.empty() ? rmd2->get_position() : this->x_end(this->schedule2.back());
    double v0 = this->schedule1.empty() ? rmd1->get_speed() : this->v_end(this->schedule1.back());
    double w0 = this->schedule2.empty() ? rmd2->get_speed() : this->v_end(this->schedule2.back());
    TrajectoryTriple t1 = this->compute_trajectory(x0, x, v0, v);
    TrajectoryTriple t2 = this->compute_trajectory(y0, y, w0, w);
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

void RmdPair::stop() {
    this->schedule1.clear();
    this->schedule2.clear();
    this->rmd1->stop();
    this->rmd2->stop();
}

void RmdPair::step() {
    const double t = millis() / 1000.0;
    while (!this->schedule1.empty() && this->t_end(this->schedule1.front()) < t) {
        this->schedule1.pop_front();
        if (this->schedule1.empty()) {
            this->rmd1->stop();
        }
    }
    while (!this->schedule2.empty() && this->t_end(this->schedule2.front()) < t) {
        this->schedule2.pop_front();
        if (this->schedule2.empty()) {
            this->rmd2->stop();
        }
    }
    const double dt = this->properties.at("dt")->number_value;
    if (!this->schedule1.empty()) {
        const double target_position = this->x(this->schedule1.front(), t);
        const double d_position = target_position - rmd1->get_position();
        if (std::abs(d_position) > this->properties.at("max_error")->number_value) {
            echo("error: \"%s\" position difference too large", rmd1->name.c_str());
            this->stop();
            return;
        }
        rmd1->speed((this->x(this->schedule1.front(), t + dt) - rmd1->get_position()) / dt);
    }
    if (!this->schedule2.empty()) {
        const double target_position = this->x(this->schedule2.front(), t);
        const double d_position = target_position - rmd2->get_position();
        if (std::abs(d_position) > this->properties.at("max_error")->number_value) {
            echo("error: \"%s\" position difference too large", rmd2->name.c_str());
            this->stop();
            return;
        }
        rmd2->speed((this->x(this->schedule2.front(), t + dt) - rmd2->get_position()) / dt);
    }
    Module::step();
}

void RmdPair::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "move") {
        if (arguments.size() == 2) {
            Module::expect(arguments, 2, numbery, numbery);
            this->schedule_trajectories(
                arguments[0]->evaluate_number(),
                arguments[1]->evaluate_number(),
                0,
                0);
        } else {
            Module::expect(arguments, 4, numbery, numbery, numbery, numbery);
            this->schedule_trajectories(
                arguments[0]->evaluate_number(),
                arguments[1]->evaluate_number(),
                arguments[2]->evaluate_number(),
                arguments[3]->evaluate_number());
        }
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->stop();
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
