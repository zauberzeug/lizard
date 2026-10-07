#pragma once

#include "line_queue.h"
#include <cstddef>
#include <functional>
#include <string>

// Sends console lines over a BLE link from its own task, in chunks of the link's payload size,
// so that a main loop that blocks while printing on UART0 does not stall the link.
class BleLineStream {
public:
    using Ready = std::function<bool()>;
    using ChunkSize = std::function<size_t()>;
    using Send = std::function<int(const char *data, size_t len)>; // 0, or a NimBLE error when out of buffers

    BleLineStream(const char *task_name, size_t max_lines, size_t max_bytes, Ready ready, ChunkSize chunk_size, Send send);

    // queues a copy of the line plus its line end; may run on any task, false if the queue is full
    bool push(const char *line, size_t len);

private:
    LineQueue queue;
    std::string pending; // bytes taken from the queue and not sent yet, allocated once
    const Ready ready;
    const ChunkSize chunk_size;
    const Send send;

    static void run(void *arg);
};
