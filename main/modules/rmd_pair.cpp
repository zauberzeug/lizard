#include "rmd_pair.h"

RmdPair::RmdPair(const std::string name, const RmdMotor_ptr rmd1, const RmdMotor_ptr rmd2)
    : Module(rmd_pair, name), rmd1(rmd1), rmd2(rmd2) {
}

void RmdPair::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "move") {
        Module::expect(arguments, 2, numbery, numbery);
        this->rmd1->position(arguments[0]->evaluate_number());
        this->rmd2->position(arguments[1]->evaluate_number());
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
