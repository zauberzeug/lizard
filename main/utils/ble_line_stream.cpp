#include "ble_line_stream.h"
#include "freertos/task.h"
#include "uart.h"
#include <algorithm>
#include <memory>
#include <stdexcept>

static constexpr size_t MAX_PENDING = 2048; // bytes taken from the queue before sending them

BleLineStream::BleLineStream(const char *task_name, size_t max_lines, size_t max_bytes, Ready ready, ChunkSize chunk_size,
                             Send send)
    : queue(max_lines, max_bytes), ready(ready), chunk_size(chunk_size), send(send) {
    this->pending.reserve(MAX_PENDING + CONSOLE_LINE_SIZE + 1); // appending a line never reallocates
    if (xTaskCreate(run, task_name, 4096, this, 5, nullptr) != pdPASS) {
        throw std::runtime_error("could not start the BLE send task");
    }
}

bool BleLineStream::push(const char *line, size_t len) {
    return this->queue.push_copy(line, len, '\n');
}

void BleLineStream::run(void *arg) {
    BleLineStream *stream = static_cast<BleLineStream *>(arg);
    std::string &pending = stream->pending;
    while (true) {
        if (pending.empty()) {
            if (const std::unique_ptr<char[]> line = stream->queue.pop(portMAX_DELAY)) {
                pending += line.get();
            }
        }
        while (pending.size() < MAX_PENDING) {
            const std::unique_ptr<char[]> line = stream->queue.pop();
            if (!line) {
                break;
            }
            pending += line.get(); // several short lines share one chunk
        }
        if (!stream->ready()) {
            pending.clear(); // lines for a closed link are not delivered later
            continue;
        }
        const size_t chunk_size = stream->chunk_size();
        size_t sent = 0;
        while (sent < pending.size()) {
            const size_t len = std::min(chunk_size, pending.size() - sent);
            if (stream->send(pending.data() + sent, len) != 0) {
                break;
            }
            sent += len;
        }
        pending.erase(0, sent);
        if (!pending.empty()) {
            vTaskDelay(1); // out of buffers: let the controller transmit
        }
    }
}
