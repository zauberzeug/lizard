#pragma once

#include "../compilation/expression.h"
#include <vector>

// Binary telemetry frames: each frame is sent as one "~<base64>@xx" line and described by "__LAYOUT__v1" lines.
namespace telemetry {

void add_frame(const std::vector<ConstExpression_ptr> &arguments);
void announce();
void clear();
void step(); // at the end of each main loop cycle

} // namespace telemetry
