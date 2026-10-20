#pragma once

#include "../compilation/routine.h"
#include <cstdint>

namespace scheduler {

void init();
void add(const int64_t deadline_us, const Routine_ptr routine);
void clear();

} // namespace scheduler
