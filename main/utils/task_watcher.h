#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace TaskWatcher {
void print_tasks();
void print_task_info(TaskHandle_t task);
} // namespace TaskWatcher