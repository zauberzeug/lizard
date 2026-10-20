#pragma once

namespace boot_guard {

// Counts boots that died before the main loop settled and decides whether the startup script may run.
bool should_run_startup();

// Records why the startup script failed and restarts; the next boot tries again or skips the script.
[[noreturn]] void startup_failed(const char *reason);

// Called from the main loop; clears the boot counter once the firmware has run for a few seconds.
void step();

} // namespace boot_guard
