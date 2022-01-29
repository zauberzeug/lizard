#include "routine.h"

Routine::Routine(const std::vector<Action_ptr> actions)
    : actions(actions) {
}

bool Routine::is_running() const {
    return 0 <= this->instruction_index && this->instruction_index < this->actions.size();
}

void Routine::start() {
    this->instruction_index = 0;
}

void Routine::step() {
    if (!this->is_running()) {
        return;
    }
    while (this->instruction_index < this->actions.size()) {
        const bool can_proceed = this->actions[this->instruction_index]->run();
        if (!can_proceed) {
            return;
        }
        this->instruction_index++;
    }
    this->instruction_index = -1;
}
