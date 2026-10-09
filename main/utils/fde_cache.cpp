#include "freertos/FreeRTOS.h"
#include <cstdint>

// mirrors libgcc's unwind-dw2-fde.h
struct dwarf_eh_bases {
    void *tbase;
    void *dbase;
    void *func;
};

extern "C" const void *__real__Unwind_Find_FDE(void *pc, dwarf_eh_bases *bases);

namespace {

constexpr int CACHE_SIZE = 32; // a throw unwinds a handful of frames, so this holds several different errors

struct Entry {
    void *pc;
    const void *fde;
    dwarf_eh_bases bases;
    uint32_t last_use;
};

Entry cache[CACHE_SIZE];
uint32_t use_count = 0;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

} // namespace

// libgcc searches the unwind table linearly for every frame of a throw, so a throw takes tens of milliseconds on an ESP32;
// an error that repeats in every loop cycle unwinds the same frames each time, so their table entries are kept here
extern "C" const void *__wrap__Unwind_Find_FDE(void *pc, dwarf_eh_bases *bases) {
    portENTER_CRITICAL(&lock);
    for (Entry &entry : cache) {
        if (entry.fde && entry.pc == pc) {
            entry.last_use = ++use_count;
            *bases = entry.bases;
            const void *fde = entry.fde;
            portEXIT_CRITICAL(&lock);
            return fde;
        }
    }
    portEXIT_CRITICAL(&lock);

    const void *fde = __real__Unwind_Find_FDE(pc, bases);
    if (fde) {
        // replace the least recently used entry, so the unwinder's own frames, which every throw needs, stay
        portENTER_CRITICAL(&lock);
        Entry *oldest = &cache[0];
        for (Entry &entry : cache) {
            if (entry.last_use < oldest->last_use) {
                oldest = &entry;
            }
        }
        *oldest = {pc, fde, *bases, ++use_count};
        portEXIT_CRITICAL(&lock);
    }
    return fde;
}
