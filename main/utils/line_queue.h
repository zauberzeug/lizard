#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <atomic>
#include <cstddef>
#include <memory>

// Console lines handed between tasks, bounded in lines and in bytes, so that a burst cannot exhaust the heap.
class LineQueue {
public:
    LineQueue(size_t max_lines, size_t max_bytes);

    // takes over a NUL-terminated line of `len` bytes; false if the queue is full, which drops the line
    bool push(std::unique_ptr<char[]> line, size_t len);
    // queues a copy of `len` bytes followed by `end`; false if the queue is full
    bool push_copy(const char *data, size_t len, const char *end = "");
    // the next line or nullptr after `wait` ticks; `len` receives its length
    std::unique_ptr<char[]> pop(TickType_t wait = 0, size_t *len = nullptr);

private:
    struct Entry {
        char *line;
        size_t len;
    };

    QueueHandle_t queue;
    const size_t max_bytes;
    std::atomic<size_t> bytes{0};
};
