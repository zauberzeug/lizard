#pragma once

#include <stdint.h>
#include <memory>

class Motor;
using Motor_ptr = std::shared_ptr<Motor>;

class Motor {

public:
    virtual bool is_running() = 0;
    virtual void stop() = 0;
    virtual double position() = 0;
    virtual void position(const double position, const double speed, const uint32_t acceleration) = 0;
    virtual double speed() = 0;
    virtual void speed(const double speed, const uint32_t acceleration) = 0;
};