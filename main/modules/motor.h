#pragma once

#include <stdint.h>
#include <memory>

class Motor;
using Motor_ptr = std::shared_ptr<Motor>;

class Motor {

public:
    virtual bool is_running() = 0;
    virtual void stop() = 0;
    virtual int32_t position() = 0;
    virtual void position(const int32_t position, const int32_t speed, const uint32_t acceleration) = 0;
    virtual int32_t speed() = 0;
    virtual void speed(const int32_t speed, const uint32_t acceleration) = 0;
};