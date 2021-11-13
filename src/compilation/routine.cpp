#include "routine.h"

void Routine::run()
{
    for (auto const &action : this->actions)
    {
        action->run();
    }
}