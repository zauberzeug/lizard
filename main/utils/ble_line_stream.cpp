#include "ble_line_stream.h"
#include "freertos/task.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

static constexpr size_t MAX_PENDING = 2048; // bytes taken from the queue before sending them

BleLineStream::BleLineStream(const char *task_name, size_t queue_length, Ready ready, ChunkSize chunk_size, Send send)
    : ready(ready), chunk_size(chunk_size), send(send) {
    if (!(this->queue = xQueueCreate(queue_length, sizeof(char *)))) {
        throw std::runtime_error("could not allocate the BLE line queue");
    }
    if (xTaskCreate(run, task_name, 4096, this, 5, nullptr) != pdPASS) {
        throw std::runtime_error("could not start the BLE send task");
    }
}

bool BleLineStream::push(const char *line, size_t len) {
    char *copy = new (std::nothrow) char[len + 2];
    if (copy == nullptr) {
        return false;
    }
    memcpy(copy, line, len);
    copy[len] = '\n';
    copy[len + 1] = '\0';
    if (xQueueSend(this->queue, &copy, 0) != pdTRUE) {
        delete[] copy;
        return false;
    }
    return true;
}

void BleLineStream::run(void *arg) {
    BleLineStream *stream = static_cast<BleLineStream *>(arg);
    std::string pending;
    char *raw;
    while (true) {
        if (pending.empty() && xQueueReceive(stream->queue, &raw, portMAX_DELAY) == pdTRUE) {
            const std::unique_ptr<char[]> line(raw);
            pending += line.get();
        }
        while (pending.size() < MAX_PENDING && xQueueReceive(stream->queue, &raw, 0) == pdTRUE) {
            const std::unique_ptr<char[]> line(raw);
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
