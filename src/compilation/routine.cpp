#include "routine.h"

Routine::Routine(std::list<Action *> actions)
{
    this->actions = actions;
}

void Routine::run()
{
    for (auto const &action : this->actions)
    {
        action->run();
    }
}