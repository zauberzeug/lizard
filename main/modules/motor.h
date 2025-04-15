#pragma once

#include <memory>
#include <stdint.h>

class Motor;
using Motor_ptr = std::shared_ptr<Motor>;

class Motor {
public:
    virtual void stop() = 0;
    virtual double get_position() = 0;
    virtual void position(const double position, const double speed, const double acceleration) = 0;
    virtual double get_speed() = 0;
    virtual void speed(const double speed, const double acceleration) = 0;
    virtual void enable() = 0;
    virtual void disable() = 0;
};
