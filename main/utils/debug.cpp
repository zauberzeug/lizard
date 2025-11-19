#include "debug.h"
#include "uart.h"
#include "esp_heap_caps.h"

namespace debug {
void print_memory_usage() {
    const size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t min_free_internal = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    const size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    echo("heap free: %u bytes (min %u) | spiram free: %u bytes", static_cast<unsigned>(free_internal),
         static_cast<unsigned>(min_free_internal), static_cast<unsigned>(free_spiram));
}
} // namespace debug
