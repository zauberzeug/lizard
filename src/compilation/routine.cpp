#include "routine.h"

Routine::Routine(std::vector<Action *> actions)
{
    this->actions = actions;
}

Routine::~Routine()
{
    for (auto a : this->actions)
    {
        delete a;
    }
    this->actions.clear();
}

bool Routine::is_running()
{
    return 0 <= this->instruction_index && this->instruction_index < this->actions.size();
}

void Routine::start()
{
    this->instruction_index = 0;
}

void Routine::step()
{
    if (!this->is_running())
    {
        return;
    }
    while (this->instruction_index < this->actions.size())
    {
        bool can_proceed = this->actions[this->instruction_index]->run();
        if (!can_proceed)
        {
            return;
        }
        this->instruction_index++;
    }
    this->instruction_index = -1;
}
