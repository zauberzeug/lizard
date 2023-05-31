#include "rmd_pair.h"
#include "utils/timing.h"
#include "utils/uart.h"
#include <math.h>

RmdPair::RmdPair(const std::string name, const RmdMotor_ptr rmd1, const RmdMotor_ptr rmd2)
    : Module(rmd_pair, name), rmd1(rmd1), rmd2(rmd2) {
    this->properties["v_max"] = std::make_shared<NumberVariable>(360);
    this->properties["a_max"] = std::make_shared<NumberVariable>(10000);
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

void RmdPair::throttle(TrajectoryPart &part, double factor) const {
    part.t0 *= factor;
    part.v0 /= factor;
    part.a /= factor * factor;
    part.dt *= factor;
}

void RmdPair::move(double x, double y) {
    TrajectoryTriple t1 = this->compute_trajectory(rmd1->get_position(), x, 0, 0);
    TrajectoryTriple t2 = this->compute_trajectory(rmd2->get_position(), y, 0, 0);
    double duration1 = t1.part_a.dt + t1.part_b.dt + t1.part_c.dt;
    double duration2 = t2.part_a.dt + t2.part_b.dt + t2.part_c.dt;
    double duration = std::max(duration1, duration2);
    throttle(t1.part_a, duration / duration1);
    throttle(t1.part_b, duration / duration1);
    throttle(t1.part_c, duration / duration1);
    throttle(t2.part_a, duration / duration2);
    throttle(t2.part_b, duration / duration2);
    throttle(t2.part_c, duration / duration2);
    this->rmd1->set_acceleration(0, std::abs(t1.part_a.a));
    this->rmd1->set_acceleration(1, std::abs(t1.part_c.a));
    this->rmd2->set_acceleration(0, std::abs(t2.part_a.a));
    this->rmd2->set_acceleration(1, std::abs(t2.part_c.a));
    this->rmd1->position(x, std::abs(t1.part_b.v0));
    this->rmd2->position(y, std::abs(t2.part_b.v0));
}

void RmdPair::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "move") {
        Module::expect(arguments, 2, numbery, numbery);
        this->move(arguments[0]->evaluate_number(), arguments[1]->evaluate_number());
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->rmd1->stop();
        this->rmd2->stop();
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
