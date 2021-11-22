#include "await_routine.h"

AwaitRoutine::AwaitRoutine(Routine *routine)
{
    this->routine = routine;
}

bool AwaitRoutine::run()
{
    if (!this->routine->is_running())
    {
        this->routine->start();
    }
    this->routine->step();
    return !this->routine->is_running();
}