#include "scheduler.h"
#include "../modules/core.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "interpreter_lock.h"
#include "uart.h"
#include <algorithm>
#include <queue>
#include <stdexcept>
#include <vector>

extern Core_ptr core_module;

namespace scheduler {

constexpr size_t MAX_ENTRIES = 100;

struct Entry {
    int64_t deadline_us;
    uint32_t sequence; // FIFO tiebreaker for equal deadlines (the heap is not stable)
    Routine_ptr routine;
};

struct EntryCompare {
    bool operator()(const Entry &a, const Entry &b) const {
        return a.deadline_us != b.deadline_us ? a.deadline_us > b.deadline_us : a.sequence > b.sequence;
    }
};

static std::priority_queue<Entry, std::vector<Entry>, EntryCompare> entries;
static uint32_t next_sequence = 0;
static esp_timer_handle_t timer = nullptr;
static TaskHandle_t task = nullptr;

static void arm_timer() {
    esp_timer_stop(timer); // ignore the error if the timer is not running
    if (!entries.empty()) {
        const int64_t delay_us = std::max<int64_t>(0, entries.top().deadline_us - esp_timer_get_time());
        ESP_ERROR_CHECK(esp_timer_start_once(timer, delay_us));
    }
}

static void run_due_entries() {
    InterpreterLock lock;
    while (!entries.empty() && entries.top().deadline_us <= esp_timer_get_time()) {
        const Entry entry = entries.top();
        entries.pop();
        if (core_module->get_property("debug")->boolean_value) {
            echo("at %lld: fired %.1f ms late",
                 entry.deadline_us / 1000, (esp_timer_get_time() - entry.deadline_us) / 1000.0);
        }
        try {
            entry.routine->start();
            entry.routine->step(); // without awaits the routine completes in a single step
        } catch (const std::runtime_error &e) {
            echo("error in scheduled block: %s", e.what());
        }
    }
    arm_timer();
}

static void scheduler_task(void *) {
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        run_due_entries();
    }
}

static void timer_callback(void *) {
    xTaskNotifyGive(task);
}

void init() {
    const esp_timer_create_args_t timer_args = {
        .callback = timer_callback,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "scheduler",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    if (xTaskCreatePinnedToCore(scheduler_task, "scheduler", 8192, nullptr, 10, &task, 0) != pdPASS) {
        throw std::runtime_error("failed to create scheduler task");
    }
}

void add(const int64_t deadline_us, const Routine_ptr routine) {
    InterpreterLock lock;
    if (entries.size() >= MAX_ENTRIES) {
        throw std::runtime_error("schedule is full");
    }
    entries.push({deadline_us, next_sequence++, routine});
    arm_timer();
}

void clear() {
    InterpreterLock lock;
    entries = {};
    esp_timer_stop(timer); // ignore the error if the timer is not running
}

} // namespace scheduler
