#include "routine.h"

Routine::Routine(const std::vector<Action_ptr> actions)
    : actions(actions) {
}

bool Routine::is_running() const {
    return 0 <= this->instruction_index && this->instruction_index < this->actions.size();
}

bool Routine::has_failed() const {
    return this->failed;
}

void Routine::start() {
    this->instruction_index = 0;
    this->failed = false;
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

void Routine::fail() {
    this->instruction_index = -1;
    this->failed = true;
}
