#pragma once

#include <cstdint>
#include <cstring>

template <size_t SIZE>
class RingBuffer {
private:
    uint8_t buffer[SIZE];
    volatile size_t write_pos = 0;
    volatile size_t read_pos = 0;
    volatile size_t count = 0;

public:
    bool write(const uint8_t *data, size_t len) {
        if (len > SIZE - count) {
            return false; // Buffer would overflow
        }

        for (size_t i = 0; i < len; i++) {
            buffer[write_pos] = data[i];
            write_pos = (write_pos + 1) % SIZE;
        }
        count += len;
        return true;
    }

    bool write(uint8_t byte) {
        if (count >= SIZE) {
            return false; // Buffer is full
        }

        buffer[write_pos] = byte;
        write_pos = (write_pos + 1) % SIZE;
        count++;
        return true;
    }

    int read() {
        if (count == 0) {
            return -1; // Buffer is empty
        }

        uint8_t byte = buffer[read_pos];
        read_pos = (read_pos + 1) % SIZE;
        count--;
        return byte;
    }

    size_t read(uint8_t *data, size_t len) {
        if (len > count) {
            len = count; // Read only what's available
        }

        for (size_t i = 0; i < len; i++) {
            data[i] = buffer[read_pos];
            read_pos = (read_pos + 1) % SIZE;
        }
        count -= len;
        return len;
    }

    size_t available() const {
        return count;
    }

    size_t free_space() const {
        return SIZE - count;
    }

    void clear() {
        write_pos = 0;
        read_pos = 0;
        count = 0;
    }

    // Find the position of a pattern in the buffer
    int find_pattern(uint8_t pattern) const {
        if (count == 0)
            return -1;

        size_t pos = read_pos;
        for (size_t i = 0; i < count; i++) {
            if (buffer[pos] == pattern) {
                return i;
            }
            pos = (pos + 1) % SIZE;
        }
        return -1;
    }

    // Peek at data without removing it
    int peek(size_t offset = 0) const {
        if (offset >= count)
            return -1;
        return buffer[(read_pos + offset) % SIZE];
    }
};