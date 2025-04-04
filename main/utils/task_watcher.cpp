#include "task_watcher.h"
#include "string_utils.h"
#include "uart.h"
#include <array>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace TaskWatcher {

void print_tasks() {
    UBaseType_t task_count = uxTaskGetNumberOfTasks();

    // Buffer for task list - needs to be large enough for all task info
    static char task_list_buffer[1024];

    echo("Task Statistics:");
    echo("----------------------------------------");
    echo("Total number of tasks: %u\n", task_count);

    // Get and print detailed task list
    vTaskList(task_list_buffer);
    echo("Name          State  Prio   Stack  Num");
    echo("----------------------------------------");
    echo("%s", task_list_buffer);
    echo("----------------------------------------");

    // Print heap info
    echo("\nMemory Statistics:");
    echo("Free heap size: %u bytes", xPortGetFreeHeapSize());
    echo("Minimum ever free heap: %u bytes", xPortGetMinimumEverFreeHeapSize());

#if configGENERATE_RUN_TIME_STATS == 1
    // Get and print runtime stats if enabled
    vTaskGetRunTimeStats(task_list_buffer);
    echo("\nTask Runtime Statistics (absolute time):");
    echo("----------------------------------------");
    echo("Name          Time      Percentage");
    echo("----------------------------------------");
    echo("%s", task_list_buffer);
#endif
}

} // namespace TaskWatcher