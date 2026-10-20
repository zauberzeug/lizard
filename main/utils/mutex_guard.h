#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// RAII guard for a FreeRTOS mutex, blocking until the mutex is taken.
class MutexGuard {
public:
    explicit MutexGuard(SemaphoreHandle_t mutex) : mutex(mutex) { xSemaphoreTake(this->mutex, portMAX_DELAY); }
    ~MutexGuard() { xSemaphoreGive(this->mutex); }

    MutexGuard(const MutexGuard &) = delete;
    MutexGuard &operator=(const MutexGuard &) = delete;

private:
    const SemaphoreHandle_t mutex;
};
