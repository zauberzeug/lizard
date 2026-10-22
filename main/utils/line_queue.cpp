#include "line_queue.h"
#include <cstring>
#include <new>
#include <stdexcept>

LineQueue::LineQueue(size_t max_lines, size_t max_bytes) : max_bytes(max_bytes) {
    if (!(this->queue = xQueueCreate(max_lines, sizeof(Entry)))) {
        throw std::runtime_error("could not allocate a line queue");
    }
}

bool LineQueue::push(std::unique_ptr<char[]> line, size_t len) {
    if (this->bytes.fetch_add(len) + len > this->max_bytes) {
        this->bytes.fetch_sub(len);
        return false;
    }
    const Entry entry{line.get(), len};
    if (xQueueSend(this->queue, &entry, 0) != pdTRUE) {
        this->bytes.fetch_sub(len);
        return false;
    }
    line.release();
    return true;
}

bool LineQueue::push_copy(const char *data, size_t len, const char *end) {
    const size_t end_len = strlen(end);
    std::unique_ptr<char[]> line(new (std::nothrow) char[len + end_len + 1]);
    if (!line) {
        return false;
    }
    memcpy(line.get(), data, len);
    memcpy(line.get() + len, end, end_len + 1);
    return this->push(std::move(line), len + end_len);
}

std::unique_ptr<char[]> LineQueue::pop(TickType_t wait, size_t *len) {
    Entry entry;
    if (xQueueReceive(this->queue, &entry, wait) != pdTRUE) {
        return nullptr;
    }
    this->bytes.fetch_sub(entry.len);
    if (len != nullptr) {
        *len = entry.len;
    }
    return std::unique_ptr<char[]>(entry.line);
}
