#include "interpreter_lock.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t get_mutex() {
    static const SemaphoreHandle_t mutex = xSemaphoreCreateRecursiveMutex();
    return mutex;
}

InterpreterLock::InterpreterLock() {
    xSemaphoreTakeRecursive(get_mutex(), portMAX_DELAY);
}

InterpreterLock::~InterpreterLock() {
    xSemaphoreGiveRecursive(get_mutex());
}
